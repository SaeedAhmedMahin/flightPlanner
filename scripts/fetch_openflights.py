#!/usr/bin/env python3
"""
fetch_openflights.py

Fetches and cleans OpenFlights airport and route data, simulating ~40 million
realistic commercial flights from February 2027 to December 2027 based on
empirical aviation economics:
1. Passenger Yield Model (sub-linear economy-of-scale distance fare):
   Base Fare = ($22 fixed terminal/dispatch + $1.10*sqrt(km) + $0.025/km) * Airline_Multiplier
2. Airport Taxes & Fees: $15 domestic, $45 international (customs, immigration, security)
3. Dynamic Pricing: Seasonal demand curves (Feb-Dec 2027), day-of-week demand, & time-of-day curves
4. Aircraft Flight Duration: 800 km/h cruise speed + 40 min taxi/climb/descent
5. Global Daily Schedules (~40M flights across 334 days):
   - Mega hub multi-carrier corridors (min_deg >= 250, >=4 carriers): 10 daily flights
   - Major international gateway corridors (min_deg >= 120): 4-8 daily flights (DAC <-> BKK 8/day)
   - Medium regional corridors (50..119): 3 daily flights
   - Small regional corridors (20..49): 2 daily flights
   - Local spoke corridors (<20): 1 daily flight
"""

import csv
import datetime
import hashlib
import math
import os
import random
import urllib.request
from collections import Counter, defaultdict

AIRPORTS_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/airports.dat"
ROUTES_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/routes.dat"
AIRLINES_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/airlines.dat"

# Famous Airlines mapped to pricing multipliers (calibrated to real-world economy yields)
AIRLINE_TIERS = {
    # Premium Full-Service Carriers (1.12 to 1.25)
    "EK": 1.25, "SQ": 1.25, "QR": 1.25, "EY": 1.20, "CX": 1.20,
    "JL": 1.18, "NH": 1.18, "BA": 1.15, "LH": 1.15, "AF": 1.15,
    "LX": 1.15, "QF": 1.18, "NZ": 1.15, "KE": 1.12, "OZ": 1.12,
    "BR": 1.12, "VS": 1.15,

    # Standard / Legacy Carriers (0.85 to 1.05)
    "AA": 1.02, "DL": 1.05, "UA": 1.02, "AC": 1.02, "KL": 1.05,
    "IB": 0.98, "SK": 1.00, "AY": 1.00, "TP": 0.95, "TK": 1.00,
    "MS": 0.90, "SA": 0.92, "MH": 0.95, "TG": 0.98, "PG": 0.94,
    "VN": 0.92, "PR": 0.90, "AI": 0.90, "BG": 0.88, "BS": 0.85,
    "KB": 0.95, "LA": 0.98, "AR": 0.92, "AM": 0.95,

    # Budget / Low-Cost Carriers (0.60 to 0.80)
    "WN": 0.80, "B6": 0.80, "NK": 0.65, "F9": 0.65, "FR": 0.60,
    "U2": 0.68, "W6": 0.62, "DY": 0.72, "AK": 0.65, "FD": 0.68,
    "TR": 0.68, "3K": 0.70, "JQ": 0.70, "G9": 0.68, "FZ": 0.72,
    "6E": 0.68, "SG": 0.68, "VY": 0.70, "OD": 0.74, "VQ": 0.78
}

# Active modern direct routes that were sparse/missing in the historical OpenFlights snapshot
MODERN_SUPPLEMENT_ROUTES = [
    ("DAC", "BKK", "BS", "738"), ("BKK", "DAC", "BS", "738"),
    ("DAC", "BKK", "BG", "789"), ("BKK", "DAC", "BG", "789"),
    ("DAC", "BKK", "TG", "359"), ("BKK", "DAC", "TG", "359"),
    ("DAC", "BKK", "PG", "320"), ("BKK", "DAC", "PG", "320"),
    ("DAC", "DMK", "FD", "320"), ("DMK", "DAC", "FD", "320"),
    ("DAC", "SIN", "BS", "738"), ("SIN", "DAC", "BS", "738"),
    ("DAC", "KUL", "BS", "738"), ("KUL", "DAC", "BS", "738"),
    ("DAC", "DXB", "BS", "738"), ("DXB", "DAC", "BS", "738"),
    ("DAC", "DOH", "BS", "738"), ("DOH", "DAC", "BS", "738"),
    ("DAC", "CCU", "BS", "AT7"), ("CCU", "DAC", "BS", "AT7"),
    ("DAC", "CAN", "BS", "738"), ("CAN", "DAC", "BS", "738"),
    ("DAC", "MAA", "BS", "738"), ("MAA", "DAC", "BS", "738"),
    ("SIN", "JFK", "SQ", "359"), ("JFK", "SIN", "SQ", "359"),
]


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
    a = math.sin((lat2 - lat1) / 2.0) ** 2 + math.cos(lat1) * math.cos(lat2) * math.sin((lon2 - lon1) / 2.0) ** 2
    return R * 2.0 * math.asin(math.sqrt(a))


def stable_hash_int(key_str):
    return int(hashlib.md5(key_str.encode()).hexdigest(), 16)


def get_airline_multiplier(airline_code):
    code_str = str(airline_code).strip()
    if code_str in AIRLINE_TIERS:
        return AIRLINE_TIERS[code_str]
    hash_val = stable_hash_int(code_str)
    return 0.85 + (hash_val % 21) / 100.0


def get_tod_multiplier_fast(departure_time_mins, rng_val):
    hour = departure_time_mins / 60.0
    if hour >= 23.0 or hour < 5.0:
        return 0.85 + rng_val * 0.07
    elif 5.0 <= hour < 7.0:
        return 0.90 + rng_val * 0.06
    elif 7.0 <= hour < 9.5:
        return 1.08 + rng_val * 0.10
    elif 9.5 <= hour < 16.5:
        return 0.96 + rng_val * 0.08
    elif 16.5 <= hour < 19.5:
        return 1.08 + rng_val * 0.10
    else:
        return 0.92 + rng_val * 0.06


def get_seasonal_multiplier(flight_date):
    m = flight_date.month
    d = flight_date.day
    wd = flight_date.weekday()

    if m in (6, 7, 8) or (m == 12 and d >= 15):
        season = random.uniform(1.08, 1.18)
    elif m in (2, 3, 9, 10):
        season = random.uniform(0.90, 0.98)
    else:
        season = random.uniform(0.98, 1.05)

    if wd in (4, 6):
        dow = 1.06
    elif wd in (1, 2):
        dow = 0.95
    else:
        dow = 1.00

    return season * dow


def choose_scheduled_departure_mins(route_key, dist_km, slot_idx, total_slots):
    h = stable_hash_int(f"{route_key}:{slot_idx}")
    if dist_km < 2500.0:
        start, span = 360, 960   # 06:00 .. 22:00
    else:
        start, span = 360, 1080  # 06:00 .. 23:55
    band = max(5, span // max(1, total_slots))
    slot_start = start + slot_idx * band
    offset = ((h % band) // 5) * 5
    return ((slot_start + offset) // 5 * 5) % 1440


def process_data(raw_dir, proc_dir):
    os.makedirs(proc_dir, exist_ok=True)

    # 1. Load & Clean Airports
    print("Processing airports...")
    airports_raw_path = os.path.join(raw_dir, "airports.dat")
    clean_airports = []
    id_map = {}
    iata_map = {}

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
            if iata not in iata_map:
                iata_map[iata] = dense_id
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
    print("Processing routes and simulating ~40M realistic schedules (Feb 2027 - Dec 2027)...")
    routes_raw_path = os.path.join(raw_dir, "routes.dat")
    routes_out_path = os.path.join(proc_dir, "routes_clean.csv")

    random.seed(42)
    base_date = datetime.date(2027, 2, 1)
    end_date = datetime.date(2027, 12, 31)
    total_days = (end_date - base_date).days + 1  # 334 days (0..333)
    date_strs = [(base_date + datetime.timedelta(days=d)).strftime("%Y-%m-%d") for d in range(total_days)]
    date_objs = [base_date + datetime.timedelta(days=d) for d in range(total_days)]
    day_seasons = [get_seasonal_multiplier(dt) for dt in date_objs]

    seen_route_triples = set()
    corridors = defaultdict(list)
    airport_deg = Counter()

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
                src_id = id_map[src_of_id]
                dst_id = id_map[dst_of_id]
                if src_id == dst_id:
                    continue

                if airline == "RX" and airline_id.strip() == "19676":
                    airline = "BS"

                eq = equipment.strip()
                eq = "UNK" if (not eq or eq == "\\N") else eq.split()[0]
                # Keep unquoted safe string
                eq = eq.replace(",", "")
                airline_clean = airline.replace(",", "")

                key = (src_id, dst_id, airline_clean)
                if key not in seen_route_triples:
                    seen_route_triples.add(key)
                    corridors[(src_id, dst_id)].append((airline_clean, eq))
                    airport_deg[src_id] += 1
                    airport_deg[dst_id] += 1

    added_modern = 0
    for src_iata, dst_iata, airline, eq in MODERN_SUPPLEMENT_ROUTES:
        if src_iata in iata_map and dst_iata in iata_map:
            src_id = iata_map[src_iata]
            dst_id = iata_map[dst_iata]
            key = (src_id, dst_id, airline)
            if key not in seen_route_triples:
                seen_route_triples.add(key)
                corridors[(src_id, dst_id)].append((airline, eq))
                airport_deg[src_id] += 1
                airport_deg[dst_id] += 1
                added_modern += 1

    print(f"Indexed {len(corridors)} unique corridors across {len(seen_route_triples)} airline routes (+{added_modern} modern links).")

    flight_count = 0
    buf = []
    rand_rand = random.random
    rand_int = random.randint

    with open(routes_out_path, mode="w", buffering=8 * 1024 * 1024, encoding="utf-8") as f:
        f.write("dense_source_id,dense_dest_id,airline,equipment,price_usd,flight_date,duration_mins,departure_time,arrival_time\n")

        for (src_id, dest_id), carrier_list in corridors.items():
            src_apt = clean_airports[src_id]
            dest_apt = clean_airports[dest_id]

            dist_km = haversine(src_apt["lat"], src_apt["lon"], dest_apt["lat"], dest_apt["lon"])
            distance_fare = 22.0 + (1.10 * math.sqrt(dist_km)) + (0.025 * dist_km)
            flight_tax = 15 if src_apt["country"] == dest_apt["country"] else 45

            raw_duration = (dist_km / 800.0) * 60.0 + 40.0
            duration_mins = max(35, int(round(raw_duration / 5.0) * 5))
            tz_shift_mins = int(round((dest_apt["utc_offset"] - src_apt["utc_offset"]) * 60.0 / 5.0) * 5)

            min_deg = min(airport_deg[src_id], airport_deg[dest_id])
            num_carriers = len(carrier_list)

            # Determine daily frequencies across all 334 days -> ~40M total flights
            if min_deg >= 250 and num_carriers >= 4:
                flights_per_day = 10
            elif min_deg >= 120 and num_carriers >= 4:
                flights_per_day = 8
            elif min_deg >= 120 and num_carriers >= 2:
                flights_per_day = 6
            elif min_deg >= 120:
                flights_per_day = 4
            elif min_deg >= 50:
                flights_per_day = 3
            elif min_deg >= 20:
                flights_per_day = 2
            else:
                flights_per_day = 1

            # Precompute slot table for fast buffered generation
            slot_table = []
            for slot in range(flights_per_day):
                c_idx = slot % num_carriers
                airline, eq = carrier_list[c_idx]
                if eq == "UNK":
                    eq = "777" if dist_km >= 4500 else ("738" if dist_km >= 1000 else "AT7")
                base_fare = distance_fare * get_airline_multiplier(airline)
                base_dep = choose_scheduled_departure_mins(f"{src_id}-{dest_id}-{airline}", dist_km, slot, flights_per_day)
                slot_table.append((c_idx, base_dep))

            # Precompute per-carrier tuple for direct indexing
            c_tuples = []
            for airline, eq in carrier_list:
                if eq == "UNK":
                    eq = "777" if dist_km >= 4500 else ("738" if dist_km >= 1000 else "AT7")
                c_tuples.append((airline, eq, distance_fare * get_airline_multiplier(airline)))

            prefix = f"{src_id},{dest_id},"
            dur_str = f",{duration_mins},"

            for d_idx in range(total_days):
                f_date_str = date_strs[d_idx]
                season_mult = day_seasons[d_idx]

                for slot in range(flights_per_day):
                    c_idx = (slot + d_idx) % num_carriers
                    airline, eq, base_fare = c_tuples[c_idx]
                    base_dep = slot_table[slot][1]

                    departure_time = (base_dep + rand_int(-2, 2) * 5) % 1440
                    arrival_time = (departure_time + duration_mins + tz_shift_mins) % 1440

                    tod_mult = get_tod_multiplier_fast(departure_time, rand_rand())
                    yield_mult = 0.94 + rand_rand() * 0.13
                    ticket_price = int((base_fare * tod_mult * season_mult * yield_mult) + flight_tax)
                    if ticket_price < 20:
                        ticket_price = 20

                    buf.append(f"{prefix}{airline},{eq},{ticket_price},{f_date_str}{dur_str}{departure_time},{arrival_time}\n")
                    flight_count += 1

                if len(buf) >= 16384:
                    f.write("".join(buf))
                    buf.clear()

        if buf:
            f.write("".join(buf))
            buf.clear()

    print(f"Success. Exported {len(clean_airports)} airports and {flight_count:,} flights to {proc_dir} ({base_date} to {end_date}).")


if __name__ == "__main__":
    raw_directory = "data/raw"
    processed_directory = "data/processed"

    download_data(raw_directory)
    process_data(raw_directory, processed_directory)
