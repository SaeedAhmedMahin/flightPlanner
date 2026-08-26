#!/usr/bin/env python3
"""
fetch_openflights.py
Script to download and clean OpenFlights dataset (airports.dat, routes.dat, airlines.dat).
"""

import sys
import os
import urllib.request

AIRPORTS_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/airports.dat"
ROUTES_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/routes.dat"
AIRLINES_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/airlines.dat"

def download_data(output_dir="data/raw"):
    os.makedirs(output_dir, exist_ok=True)
    urls = {
        "airports.dat": AIRPORTS_URL,
        "routes.dat": ROUTES_URL,
        "airlines.dat": AIRLINES_URL
    }
    for filename, url in urls.items():
        dest = os.path.join(output_dir, filename)
        print(f"Downloading {filename} from {url}...")
        try:
            urllib.request.urlretrieve(url, dest)
            print(f"Saved to {dest}")
        except Exception as e:
            print(f"Error downloading {filename}: {e}", file=sys.stderr)

if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "data/raw"
    download_data(out_dir)
