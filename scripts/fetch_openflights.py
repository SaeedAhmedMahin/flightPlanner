#!/usr/bin/env python3
"""
This fetches our data, we dont have flight price access as they fluctuate a lot, 
so we are simulating it for now, using several realistic dynamic pricing factors:
1. Base price ($40) + Distance fare ($0.10/km) * Airline Premium Multiplier
2. Time-of-Day Curve: Red-eye/night flights (0.50-0.70x) vs. Morning/Evening rush (1.25-1.65x)
3. Yield Management: Random surges (1.30-1.80x) or flash discounts (0.50-0.70x)
4. Duration: 800km/h + 40 mins (taxi/takeoff/landing)
"""

import sys
import os
import urllib.request
import pandas as pd
import numpy as np
import hashlib
import random
import datetime

AIRPORTS_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/airports.dat"
ROUTES_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/routes.dat"
AIRLINES_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/airlines.dat"

# 50 Famous Airlines mapped to pricing multipliers
AIRLINE_TIERS = {
    # Premium Carriers (1.2 to 1.5)
    "EK": 1.50, "SQ": 1.50, "QR": 1.50, "EY": 1.40, "CX": 1.40, 
    "JL": 1.35, "NH": 1.35, "BA": 1.35, "LH": 1.30, "AF": 1.30, 
    "LX": 1.30, "QF": 1.35, "NZ": 1.30, "KE": 1.25, "OZ": 1.25, 
    "BR": 1.25, "VS": 1.25,

    # Standard / Legacy Carriers (0.9 to 1.15)
    "AA": 1.05, "DL": 1.10, "UA": 1.05, "AC": 1.05, "KL": 1.10, 
    "IB": 1.00, "SK": 1.05, "AY": 1.05, "TP": 0.95, "TK": 1.00, 
    "MS": 0.90, "SA": 0.95, "MH": 0.95, "TG": 1.00, "VN": 0.95, 
    "PR": 0.90, "AI": 0.90, "LA": 1.00, "AR": 0.95, "AM": 0.95,

    # Budget / Low-Cost Carriers (0.5 to 0.8)
    "WN": 0.80, "B6": 0.80, "NK": 0.55, "F9": 0.55, "FR": 0.50, 
    "U2": 0.60, "W6": 0.55, "DY": 0.65, "AK": 0.55, "TR": 0.60, 
    "3K": 0.65, "JQ": 0.65, "G9": 0.60, "FZ": 0.65, "6E": 0.60, 
    "SG": 0.60, "VY": 0.65
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
    lat1, lon1, lat2, lon2 = map(np.radians, [lat1, lon1, lat2, lon2])
    a = np.sin((lat2 - lat1)/2)**2 + np.cos(lat1) * np.cos(lat2) * np.sin((lon2 - lon1)/2)**2
    return R * 2 * np.arcsin(np.sqrt(a))

def get_airline_multiplier(airline_code):
    code_str = str(airline_code)
    if code_str in AIRLINE_TIERS:
        return AIRLINE_TIERS[code_str]
        
    # Deterministic hash fallback for unknown airlines
    hash_val = int(hashlib.md5(code_str.encode()).hexdigest(), 16)
    return 0.6 + (hash_val % 91) / 100.0

def get_time_of_day_multiplier(departure_time_mins):
    """
    Simulates airline dynamic pricing based on time-of-day demand:
    - Red-eye / Late Night (23:00 - 05:00): Moderate discount (0.80 - 0.90x)
    - Early Morning (05:00 - 07:00): Slight discount (0.85 - 0.95x)
    - Morning Peak Rush (07:00 - 09:30): Business demand surge (1.15 - 1.30x)
    - Daytime / Standard (09:30 - 16:30): Regular demand (0.95 - 1.05x)
    - Evening Peak Rush (16:30 - 19:30): Business/leisure demand (1.15 - 1.30x)
    - Late Evening (19:30 - 23:00): Slight discount (0.88 - 0.98x)
    """
    hour = departure_time_mins / 60.0
    if hour >= 23.0 or hour < 5.0:
        return random.uniform(0.80, 0.90)  # Red-eye / Night flight
    elif 5.0 <= hour < 7.0:
        return random.uniform(0.85, 0.95)  # Early morning
    elif 7.0 <= hour < 9.5:
        return random.uniform(1.15, 1.30)  # Morning rush
    elif 9.5 <= hour < 16.5:
        return random.uniform(0.95, 1.05)  # Midday regular
    elif 16.5 <= hour < 19.5:
        return random.uniform(1.15, 1.30)  # Evening rush
    else:
        return random.uniform(0.88, 0.98)  # Late evening

def process_data(raw_dir, proc_dir):
    os.makedirs(proc_dir, exist_ok=True)
    
    # 1. Load & Clean Airports
    print("Processing airports...")
    airports_cols = ["openflights_id", "name", "city", "country", "iata", "icao", "lat", "lon", "alt", "utc_offset", "dst", "tz", "type", "source"]
    df_airports = pd.read_csv(os.path.join(raw_dir, "airports.dat"), names=airports_cols, na_values=["\\N"])
    
    df_airports = df_airports.dropna(subset=["iata", "lat", "lon", "utc_offset"])
    df_airports = df_airports[df_airports["iata"].str.len() == 3]
    df_airports = df_airports.reset_index(drop=True)
    df_airports["dense_id"] = df_airports.index
    
    id_map = dict(zip(df_airports["openflights_id"], df_airports["dense_id"]))
    
    # 2. Load & Clean Routes
    print("Processing routes...")
    routes_cols = ["airline", "airline_id", "source_iata", "source_id", "dest_iata", "dest_id", "codeshare", "stops", "equipment"]
    df_routes = pd.read_csv(os.path.join(raw_dir, "routes.dat"), names=routes_cols, na_values=["\\N"])
    
    df_routes = df_routes.dropna(subset=["source_id", "dest_id", "airline"])
    df_routes["source_id"] = pd.to_numeric(df_routes["source_id"], errors='coerce')
    df_routes["dest_id"] = pd.to_numeric(df_routes["dest_id"], errors='coerce') #converts data to null if its non numeric
    df_routes = df_routes.dropna(subset=["source_id", "dest_id"])
    
    df_routes = df_routes[df_routes["source_id"].isin(id_map) & df_routes["dest_id"].isin(id_map)]
    df_routes["dense_source_id"] = df_routes["source_id"].map(id_map).astype(int)
    df_routes["dense_dest_id"] = df_routes["dest_id"].map(id_map).astype(int)
    
    # 3. Simulate Realistic Weights & Schedules
    print("Simulating realistic pricing and 5-minute schedules...")
    expanded_routes = []
    
    # Set seed for reproducible benchmark graphs
    random.seed(42)
    
    # Fixed base date for deterministic runs (e.g. 2024-01-01)
    base_date = datetime.date(2026, 11, 1)
    
    for _, row in df_routes.iterrows():
        src_id = row["dense_source_id"]
        dest_id = row["dense_dest_id"]
        
        src_lat = df_airports.loc[src_id, "lat"]
        src_lon = df_airports.loc[src_id, "lon"]
        src_utc = float(df_airports.loc[src_id, "utc_offset"])
        src_country = df_airports.loc[src_id, "country"]
        
        dest_lat = df_airports.loc[dest_id, "lat"]
        dest_lon = df_airports.loc[dest_id, "lon"]
        dest_utc = float(df_airports.loc[dest_id, "utc_offset"])
        dest_country = df_airports.loc[dest_id, "country"]
        
        dist_km = haversine(src_lat, src_lon, dest_lat, dest_lon)
        multiplier = get_airline_multiplier(row["airline"])
        
        # Base fare: fixed terminal/dispatch cost + sqrt economy of scale + per-km distance cost
        base_fare = (60.0 + (1.5 * np.sqrt(dist_km)) + (0.08 * dist_km)) * multiplier
        
        # Flight tax: $40 for domestic, $160 for international flights (customs, terminal & departure security fees)
        flight_tax = 40 if src_country == dest_country else 160
        
        raw_duration = (dist_km / 800.0) * 60 + 40
        duration_mins = int(round(raw_duration / 5.0) * 5)
        
        num_flights = random.randint(12, 22)
        
        for _ in range(num_flights):
            # Departure snapped to 5-minute increments
            departure_time = random.randint(0, 287) * 5
            
            arrival_utc_equiv = departure_time + duration_mins
            
            # Ensure time zone shift aligns to 5 mins (fixes strange OpenFlight offsets)
            tz_shift_mins = int(round((dest_utc - src_utc) * 60 / 5.0) * 5)
            raw_arrival = arrival_utc_equiv + tz_shift_mins
            
            arrival_time = raw_arrival % 1440
            
            # Generate a random flight date within a 90-day window
            flight_date_offset = random.randint(0, 89)
            flight_date = base_date + datetime.timedelta(days=flight_date_offset)
            # Formatted as YYYY-MM-DD
            flight_date_str = flight_date.strftime("%Y-%m-%d")
            
            # --- YIELD MANAGEMENT & TIME-OF-DAY PRICING ---
            tod_multiplier = get_time_of_day_multiplier(departure_time)

            rand_event = random.random()
            
            if rand_event < 0.15:
                # 15% chance: High-demand surge pricing
                event_multiplier = random.uniform(1.15, 1.35)
            elif rand_event < 0.25:
                # 10% chance: Flash sale / discount
                event_multiplier = random.uniform(0.80, 0.90)
            else:
                # 75% chance: Standard daily fluctuation
                event_multiplier = random.uniform(0.95, 1.05)

            # Base price with time-of-day and yield management surge/discount, plus baked-in flight tax
            ticket_price = int((base_fare * tod_multiplier * event_multiplier) + flight_tax)
            ticket_price = max(25, ticket_price) # Ensure ticket price never drops below $25
            # --------------------------------------------
            
            expanded_routes.append({
                "dense_source_id": src_id,
                "dense_dest_id": dest_id,
                "airline": row["airline"],
                "equipment": row["equipment"] if pd.notna(row["equipment"]) else "UNK",
                "price_usd": ticket_price,
                "flight_date": flight_date_str,
                "duration_mins": duration_mins,
                "departure_time": departure_time,
                "arrival_time": arrival_time
            })
            
    df_expanded = pd.DataFrame(expanded_routes)
    
    # 4. Export
    export_airports = df_airports[["dense_id", "name", "city", "country", "iata", "lat", "lon", "utc_offset"]]
    
    export_airports.to_csv(os.path.join(proc_dir, "airports_clean.csv"), index=False)
    df_expanded.to_csv(os.path.join(proc_dir, "routes_clean.csv"), index=False)
    print(f"Success. Exported {len(export_airports)} airports and {len(df_expanded)} flights to {proc_dir}")

if __name__ == "__main__":
    raw_directory = "data/raw"
    processed_directory = "data/processed"
    
    download_data(raw_directory)
    process_data(raw_directory, processed_directory)