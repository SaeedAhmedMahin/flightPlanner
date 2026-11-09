#!/usr/bin/env python3
"""
fetch_openflights.py

Fetches and cleans OpenFlights airport and route data, simulating realistic
commercial airline schedules and market pricing based on empirical aviation economics:
1. Passenger Yield Model (sub-linear economy-of-scale distance fare):
   Base Fare = ($22 fixed terminal/dispatch + $1.10*sqrt(km) + $0.025/km) * Airline_Multiplier
2. Airport Taxes & Fees: $15 domestic, $45 international (customs, immigration, security)
3. Dynamic Pricing: Time-of-day demand curve (red-eye vs. rush hour) & Yield Management surges/discounts
4. Aircraft Flight Duration: 800 km/h cruise speed + 40 min taxi/climb/descent
"""

import csv
import datetime
import hashlib
import math
import os
import random
import sys
import urllib.request

AIRPORTS_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/airports.dat"
ROUTES_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/routes.dat"
AIRLINES_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/airlines.dat"

# 50 Famous Airlines mapped to pricing multipliers (calibrated to real-world economy yields)
AIRLINE_TIERS = {
    # Premium Full-Service Carriers (1.12 to 1.25)
    "EK": 1.25, "SQ": 1.25, "QR": 1.25, "EY": 1.20, "CX": 1.20, 
    "JL": 1.18, "NH": 1.18, "BA": 1.15, "LH": 1.15, "AF": 1.15, 
    "LX": 1.15, "QF": 1.18, "NZ": 1.15, "KE": 1.12, "OZ": 1.12, 
    "BR": 1.12, "VS": 1.15,

    # Standard / Legacy Carriers (0.90 to 1.05)
    "AA": 1.02, "DL": 1.05, "UA": 1.02, "AC": 1.02, "KL": 1.05, 
    "IB": 0.98, "SK": 1.00, "AY": 1.00, "TP": 0.95, "TK": 1.00, 
    "MS": 0.90, "SA": 0.92, "MH": 0.95, "TG": 0.98, "VN": 0.92, 
    "PR": 0.90, "AI": 0.90, "LA": 0.98, "AR": 0.92, "AM": 0.95,

    # Budget / Low-Cost Carriers (0.60 to 0.80)
    "WN": 0.80, "B6": 0.80, "NK": 0.65, "F9": 0.65, "FR": 0.60, 
    "U2": 0.68, "W6": 0.62, "DY": 0.72, "AK": 0.65, "TR": 0.68, 
    "3K": 0.70, "JQ": 0.70, "G9": 0.68, "FZ": 0.72, "6E": 0.68, 
    "SG": 0.68, "VY": 0.70
}

def download_data(output_dir):
    os.makedirs(output_dir, exist_ok=True)
    urls = {
        "airports.dat": AIRPORTS_URL,
        "routes.dat": ROUTES_URL,
        "airlines.dat": AIRLINES_URL
    }
    for filename, url in urls.items():
        dest = os.path.join(output_dir, filename)
        if not os.path.exists(dest):
            print(f"Downloading {filename}...")
            urllib.request.urlretrieve(url, dest)
        else:
            print(f"Found {filename}, skipping download.")

def haversine(lat1, lon1, lat2, lon2):
    R = 6371.0 
    lat1, lon1, lat2, lon2 = map(math.radians, [lat1, lon1, lat2, lon2])
    a = math.sin((lat2 - lat1) / 2.0)**2 + math.cos(lat1) * math.cos(lat2) * math.sin((lon2 - lon1) / 2.0)**2
    return R * 2.0 * math.asin(math.sqrt(a))

def get_airline_multiplier(airline_code):
    code_str = str(airline_code).strip()
    if code_str in AIRLINE_TIERS:
        return AIRLINE_TIERS[code_str]
        
    # Deterministic hash fallback for unknown airlines (0.85 to 1.05)
    hash_val = int(hashlib.md5(code_str.encode()).hexdigest(), 16)
    return 0.85 + (hash_val % 21) / 100.0

def get_time_of_day_multiplier(departure_time_mins):
    """
    Simulates airline dynamic pricing based on time-of-day demand:
    - Red-eye / Late Night (23:00 - 05:00): Moderate discount (0.85 - 0.92x)
    - Early Morning (05:00 - 07:00): Slight discount (0.90 - 0.96x)
    - Morning Peak Rush (07:00 - 09:30): Business demand surge (1.08 - 1.18x)
    - Daytime / Standard (09:30 - 16:30): Regular demand (0.96 - 1.04x)
    - Evening Peak Rush (16:30 - 19:30): Business/leisure demand (1.08 - 1.18x)
    - Late Evening (19:30 - 23:00): Slight discount (0.92 - 0.98x)
    """
    hour = departure_time_mins / 60.0
    if hour >= 23.0 or hour < 5.0:
        return random.uniform(0.85, 0.92)  # Red-eye / Night flight
    elif 5.0 <= hour < 7.0:
        return random.uniform(0.90, 0.96)  # Early morning
    elif 7.0 <= hour < 9.5:
        return random.uniform(1.08, 1.18)  # Morning rush
    elif 9.5 <= hour < 16.5:
        return random.uniform(0.96, 1.04)  # Midday regular
    elif 16.5 <= hour < 19.5:
        return random.uniform(1.08, 1.18)  # Evening rush
    else:
        return random.uniform(0.92, 0.98)  # Late evening

def process_data(raw_dir, proc_dir):
    os.makedirs(proc_dir, exist_ok=True)
    
    # 1. Load & Clean Airports
    print("Processing airports...")
    airports_raw_path = os.path.join(raw_dir, "airports.dat")
    clean_airports = []
    id_map = {}  # openflights_id (int) -> dense_id (int)
    
    with open(airports_raw_path, mode="r", encoding="utf-8", errors="replace") as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) < 10:
                continue
            of_id_str, name, city, country, iata, icao, lat_str, lon_str, alt, utc_str = row[:10]
            
            iata = iata.strip()
            if len(iata) != 3 or iata == "\\N":
                continue
            
            try:
                of_id = int(of_id_str)
                lat = float(lat_str)
                lon = float(lon_str)
                utc = float(utc_str)
            except ValueError:
                continue
            
            dense_id = len(clean_airports)
            id_map[of_id] = dense_id
            clean_airports.append({
                "dense_id": dense_id,
                "name": name,
                "city": city,
                "country": country,
                "iata": iata,
                "lat": lat,
                "lon": lon,
                "utc_offset": utc
            })
            
    print(f"Cleaned {len(clean_airports)} airports.")
    
    # Export clean airports
    airports_out_path = os.path.join(proc_dir, "airports_clean.csv")
    with open(airports_out_path, mode="w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["dense_id", "name", "city", "country", "iata", "lat", "lon", "utc_offset"])
        for apt in clean_airports:
            writer.writerow([
                apt["dense_id"], apt["name"], apt["city"], apt["country"],
                apt["iata"], apt["lat"], apt["lon"], apt["utc_offset"]
            ])

    # 2. Load & Clean Routes
    print("Processing routes and simulating realistic schedules & pricing...")
    routes_raw_path = os.path.join(raw_dir, "routes.dat")
    routes_out_path = os.path.join(proc_dir, "routes_clean.csv")
    
    # Set seed for reproducible benchmark graphs
    random.seed(42)
    base_date = datetime.date(2026, 11, 1)
    
    valid_routes = []
    with open(routes_raw_path, mode="r", encoding="utf-8", errors="replace") as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) < 9:
                continue
            airline, airline_id, src_iata, src_id_str, dst_iata, dst_id_str, codeshare, stops, equipment = row[:9]
            airline = airline.strip()
            if not airline or airline == "\\N":
                continue
            
            try:
                src_of_id = int(src_id_str)
                dst_of_id = int(dst_id_str)
            except ValueError:
                continue
                
            if src_of_id in id_map and dst_of_id in id_map:
                valid_routes.append({
                    "dense_src_id": id_map[src_of_id],
                    "dense_dst_id": id_map[dst_of_id],
                    "airline": airline,
                    "equipment": equipment if equipment and equipment != "\\N" else "UNK"
                })
                
    print(f"Filtered {len(valid_routes)} valid base airline routes.")
    
    # Stream simulated flight records directly to disk to minimize memory footprint
    flight_count = 0
    with open(routes_out_path, mode="w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow([
            "dense_source_id", "dense_dest_id", "airline", "equipment",
            "price_usd", "flight_date", "duration_mins", "departure_time", "arrival_time"
        ])
        
        for route in valid_routes:
            src_id = route["dense_src_id"]
            dest_id = route["dense_dst_id"]
            airline = route["airline"]
            equipment = route["equipment"]
            
            src_apt = clean_airports[src_id]
            dest_apt = clean_airports[dest_id]
            
            dist_km = haversine(src_apt["lat"], src_apt["lon"], dest_apt["lat"], dest_apt["lon"])
            multiplier = get_airline_multiplier(airline)
            
            # Realistic airline passenger yield model (sub-linear economy of scale):
            # Distance fare: fixed dispatch fee ($22) + sqrt sub-linear component + $0.025/km marginal rate
            distance_fare = 22.0 + (1.10 * math.sqrt(dist_km)) + (0.025 * dist_km)
            base_fare = distance_fare * multiplier
            
            # Airport taxes & facility fees: $15 domestic, $45 international (customs, immigration, terminal security)
            flight_tax = 15 if src_apt["country"] == dest_apt["country"] else 45
            
            raw_duration = (dist_km / 800.0) * 60.0 + 40.0
            duration_mins = int(round(raw_duration / 5.0) * 5)
            
            num_flights = random.randint(12, 22)
            
            for _ in range(num_flights):
                # Departure snapped to 5-minute increments
                departure_time = random.randint(0, 287) * 5
                arrival_utc_equiv = departure_time + duration_mins
                
                # Timezone offset in minutes
                tz_shift_mins = int(round((dest_apt["utc_offset"] - src_apt["utc_offset"]) * 60.0 / 5.0) * 5)
                raw_arrival = arrival_utc_equiv + tz_shift_mins
                arrival_time = raw_arrival % 1440
                
                # Flight date in 90-day window
                flight_date_offset = random.randint(0, 89)
                flight_date = base_date + datetime.timedelta(days=flight_date_offset)
                flight_date_str = flight_date.strftime("%Y-%m-%d")
                
                # Time of day and yield management
                tod_multiplier = get_time_of_day_multiplier(departure_time)
                rand_event = random.random()
                
                if rand_event < 0.12:
                    event_multiplier = random.uniform(1.10, 1.25)
                elif rand_event < 0.22:
                    event_multiplier = random.uniform(0.80, 0.90)
                else:
                    event_multiplier = random.uniform(0.96, 1.04)
                    
                ticket_price = int((base_fare * tod_multiplier * event_multiplier) + flight_tax)
                ticket_price = max(20, ticket_price)
                
                writer.writerow([
                    src_id, dest_id, airline, equipment,
                    ticket_price, flight_date_str, duration_mins, departure_time, arrival_time
                ])
                flight_count += 1
                
    print(f"Success. Exported {len(clean_airports)} airports and {flight_count} flights to {proc_dir}")

if __name__ == "__main__":
    raw_directory = "data/raw"
    processed_directory = "data/processed"
    
    download_data(raw_directory)
    process_data(raw_directory, processed_directory)