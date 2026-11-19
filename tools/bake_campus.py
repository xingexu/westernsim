#!/usr/bin/env python3
"""Bake a north-up campus from City of London open data and OpenStreetMap."""
import argparse
import json
import math
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import xml.etree.ElementTree as ET

import numpy as np
import requests
from PIL import Image
from pyproj import Transformer
from scipy.interpolate import LinearNDInterpolator, NearestNDInterpolator
from scipy.ndimage import distance_transform_edt, gaussian_filter
from shapely import contains_xy, make_valid, prepare
from shapely.geometry import LineString, MultiLineString, Point, Polygon, box
from shapely.ops import unary_union

EDGE, CELL = 896, 2
WEST, NORTH = 476800, 4762550
SOUTH, EAST = NORTH - EDGE * CELL, WEST + EDGE * CELL
AREA = box(WEST, SOUTH, EAST, NORTH)
CITY = "https://maps.london.ca/server/rest/services/OpenData/OpenData_BaseMaps/MapServer"
OSM = "https://api.openstreetmap.org/api/0.6/map"
PROJECT = Transformer.from_crs(4326, 26917, always_xy=True)
XX, ZZ = np.meshgrid(WEST + (np.arange(EDGE)+.5)*CELL, NORTH - (np.arange(EDGE)+.5)*CELL)

GRASS, WATER, TRAIL, ROAD, WALK, PLAZA, PARKING, STEPS, FIELD, BRIDGE, WOODS = range(11)
TOWN, STONE, MODERN, UC, MIDDLESEX, WELDON, UCC, ONTARIO, GALLERY, TALL, DELAWARE, IVEY, THAMES, MEDWAY, REC, HOSPITAL = range(1,17)
# Anchor points from Western's walking tour and the corresponding mapped building.
# Heights are visual estimates in two-metre cells. The UC tower is 126 ft.
# Name, latitude, longitude, facade, height, matching OSM way (when present).
LANDMARKS = [
    ("University College",43.00872,-81.27317,UC,8,118764092),
    ("Middlesex College",43.00935,-81.27031,MIDDLESEX,7,118764099),
    ("Weldon Library",43.00793,-81.27561,WELDON,10,118764108),
    ("University Community Centre",43.00871,-81.27612,UCC,10,141710486),
    ("Concrete Beach",43.00848,-81.27547,0,0,237301679),
    ("McIntosh Gallery",43.00949,-81.27344,GALLERY,4,141710481),
    ("Social Science Centre",43.00935,-81.27549,MODERN,13,141710484),
    ("Natural Sciences Centre",43.01064,-81.27304,MODERN,8,116779542),
    ("Physics & Astronomy",43.00972,-81.27258,STONE,8,142286122),
    ("Western Science Centre",43.00999,-81.27135,MODERN,8,118764093),
    ("Thames Hall",43.00689,-81.27449,THAMES,7,118764095),
    ("Delaware Hall",43.00866,-81.26903,DELAWARE,10,141710478),
    ("Alumni Hall",43.00599,-81.27453,STONE,8,142216959),
    ("Ontario Hall",43.00052,-81.27909,ONTARIO,12,310134768),
    ("Saugeen-Maitland Hall",43.01155,-81.27889,TALL,21,126668347),
    ("Huron University",43.00874,-81.27806,STONE,7,126668336),
    ("Ivey Business School",43.00469,-81.27831,IVEY,8,0),
    ("Student Recreation Centre",43.00317,-81.27406,REC,9,310132539),
    ("Elgin Hall",43.00827,-81.26494,DELAWARE,7,142286099),
    ("Medway Hall",43.00820,-81.26353,MEDWAY,7,0),
]
# Additional exterior treatments that don't need a numbered map pin.
SECONDARY = [
    ("University Hospital",43.0130,-81.27535,HOSPITAL,19,749120077),
    ("FIMS & Nursing",43.00601,-81.27262,MODERN,10,558927474),
    ("Music Building",43.00675,-81.27106,MODERN,9,1264685790),
    ("Amit Chakma Engineering",43.00437,-81.27636,MODERN,9,626520018),
    ("Lawson Hall",43.00897,-81.27388,STONE,7,237302165),
]
MAP_LABELS = [
    ("WESTERN ROAD",43.01290,-81.27730), ("THAMES RIVER",43.0045,-81.2696),
    ("UNIVERSITY BRIDGE",43.00771,-81.26796), ("UC HILL",43.00801,-81.27153),
]
SPAWN = 43.00832, -81.27244
MEMORIAL_TOWER = 43.00863, -81.27326
CLOCK_TOWER = 43.00918, -81.27017


def xy(lat, lon):
    return PROJECT.transform(lon, lat)


def cell(lat, lon):
    x, z = xy(lat, lon)
    return int((x-WEST)//CELL), int((NORTH-z)//CELL)


def city(cache, layer):
    path = cache / f"city-{layer}.json"
    if path.is_file():
        return json.loads(path.read_text())
    base = f"{CITY}/{layer}/query"
    params = {
        "f": "json", "where": "1=1", "geometryType": "esriGeometryEnvelope",
        "geometry": f"{WEST-50},{SOUTH-50},{EAST+50},{NORTH+50}", "inSR": 26917,
        "outSR": 26917, "orderByFields": "OBJECTID", "outFields": "OBJECTID",
    }
    if layer in (5, 37):
        params["outFields"] += ",Elevation"
    if layer == 30:
        params["maxAllowableOffset"] = .4
        params.pop("orderByFields")
    features = []
    while True:
        reply = requests.get(base, params={**params, "resultOffset":len(features),
                             "resultRecordCount":1000}, timeout=90)
        reply.raise_for_status()
        data = reply.json()
        if "error" in data:
            raise RuntimeError(f"City layer {layer}: {data['error']}")
        batch = data["features"]
        features.extend(batch)
        if len(batch) < 1000 and not data.get("exceededTransferLimit"):
            break
        if not batch:
            raise RuntimeError(f"City layer {layer} did not advance")
    path.write_text(json.dumps(features, separators=(",",":")))
    return features


def polygon(rings):
    # ArcGIS joins disjoint shells and holes with opposite winding orders.
    shells, holes = [], []
    for ring in rings:
        if len(ring) < 4:
            continue
        p = Polygon(ring)
        (holes if p.exterior.is_ccw else shells).append(make_valid(p))
    if not shells:
        return unary_union(holes)
    outline = unary_union(shells)
    return outline.difference(unary_union(holes)) if holes else outline


def mask(geometry):
    if geometry.is_empty or not geometry.intersects(AREA):
        return np.s_[:0,:0], np.zeros((0,0), dtype=bool)
    left, bottom, right, top = geometry.bounds
    xs = slice(max(0, math.floor((left-WEST)/CELL)), min(EDGE, math.ceil((right-WEST)/CELL)))
    zs = slice(max(0, math.floor((NORTH-top)/CELL)), min(EDGE, math.ceil((NORTH-bottom)/CELL)))
    dist = np.s_[zs,xs]
    prepare(geometry)
    return dist, contains_xy(geometry, XX[dist], ZZ[dist])


def paint(grid, geometry, value, where=None):
    rect, pixels = mask(geometry)
    if where is not None:
        pixels &= where[rect]
    grid[rect][pixels] = value


def osm_data(cache):
    path = cache / "osm.xml"
    if not path.is_file():
        reply = requests.get(OSM, params={"bbox":"-81.2848,42.9987,-81.2625,43.0156"}, timeout=80)
        reply.raise_for_status()
        path.write_bytes(reply.content)
    root = ET.parse(path).getroot()
    nodes = {int(node.get("id")):xy(float(node.get("lat")),float(node.get("lon"))) for node in root.iter("node")}
    for way in root.iter("way"):
        tags = {t.get("k"):t.get("v") for t in way.iter("tag")}
        points = [nodes[int(ref.get("ref"))] for ref in way.iter("nd") if int(ref.get("ref")) in nodes]
        if len(points) >= 2:
            yield int(way.get("id")), tags, points


def elevation(layers):
    samples = []
    for feature in layers[5]:
        height = feature["attributes"]["Elevation"]
        if height is None:
            continue
        for coords in feature["geometry"]["paths"]:
            line = LineString(coords).intersection(AREA.buffer(35))
            segments = [line] if line.geom_type == "LineString" else getattr(line, "geoms", ())
            for segment in segments:
                if segment.geom_type != "LineString" or segment.is_empty or segment.length == 0:
                    continue
                for d in np.linspace(0, segment.length, max(2, int(segment.length/4)+1)):
                    pt = segment.interpolate(d)
                    samples.append((pt.x,pt.y,height))
    for feature in layers[37]:
        p = feature["geometry"]
        if feature["attributes"]["Elevation"] is not None:
            samples.append((p["x"],p["y"],feature["attributes"]["Elevation"]))
    coords = np.array(samples)
    values = LinearNDInterpolator(coords[:,:2],coords[:,2])(XX,ZZ)
    missing = np.isnan(values)
    values[missing] = NearestNDInterpolator(coords[:,:2],coords[:,2])(XX[missing],ZZ[missing])
    return np.clip(np.rint((gaussian_filter(values,.65) - 224)/CELL),2,35).astype(np.uint8)


def surfaces(layers, osm, ground):
    surface = np.zeros((EDGE,EDGE), dtype=np.uint8)
    bridges = np.zeros_like(surface)
    for layer, value in [(47,WOODS),(30,ROAD),(49,WATER)]:
        for f in layers[layer]:
            paint(surface,polygon(f["geometry"]["rings"]),value)
    unpaved = np.isin(surface,[GRASS,WOODS,TRAIL,FIELD])
    dry = surface != WATER
    open_lot = dry & (surface != ROAD)
    for _, tags, points in osm:
        highway, leisure = tags.get("highway",""), tags.get("leisure","")
        if points[0] == points[-1] and len(points) >= 4:
            area = make_valid(Polygon(points))
            if tags.get("amenity") == "parking":
                paint(surface,area,PARKING,open_lot)
            elif leisure in {"pitch","track"}:
                paint(surface,area,FIELD,open_lot)
            elif tags.get("area") == "yes" and highway == "pedestrian":
                paint(surface,area,PLAZA,dry)
        radius = {"path":.9,"footway":1.35,"pedestrian":1.8,"cycleway":1.4,"steps":1.7}.get(highway)
        if radius and not tags.get("bridge") and tags.get("tunnel") != "yes":
            value = STEPS if highway == "steps" else TRAIL if highway == "path" else WALK
            paint(surface,LineString(points).buffer(radius),value,unpaved)
    for layer, value, radius in [(43,TRAIL,1.1),(11,WALK,1.4),(34,WALK,1.55),(38,STEPS,1.6)]:
        eligible = np.isin(surface,[GRASS,WOODS,TRAIL,FIELD])
        for feature in layers[layer]:
            paint(surface,MultiLineString(feature["geometry"]["paths"]).buffer(radius),value,eligible)
    by_id = {i:(t,p) for i,t,p in osm}
    _, beach = by_id[237301679]
    paint(surface,make_valid(Polygon(beach)),PLAZA,surface != WATER)

    # Road and path polygons were recorded over the same surveyed river surface.
    # Only tagged river crossings receive an elevated deck.
    water = surface == WATER
    for ident, tags, points in osm:
        if tags.get("bridge") != "yes":
            continue
        highway = tags.get("highway")
        width = {"tertiary":5.5,"secondary":6.5,"cycleway":2.0,"footway":1.35,"residential":4}.get(highway)
        if width is None:
            continue
        rect, cells = mask(LineString(points).buffer(width,cap_style="flat"))
        if not np.any(water[rect] & cells):
            continue
        ends = []
        for x,z in [points[0],points[-1]]:
            i,j = int((x-WEST)/CELL),int((NORTH-z)/CELL)
            if 0 <= i < EDGE and 0 <= j < EDGE:
                patch = ground[max(0,j-3):j+4,max(0,i-3):i+4]
                ends.append(int(np.quantile(patch,.82)))
        if not ends:
            continue
        base = max(ends)
        # Fill the approach where it crosses land, using a consistent road grade.
        approaches = cells & ~water[rect] & (ground[rect] <= base)
        ground[rect][approaches] = base
        surface[rect][approaches] = BRIDGE
        over_water = cells & water[rect]
        bridges[rect][over_water] = base
        ground[rect][over_water] = np.minimum(ground[rect][over_water],base-1)
        if ident == 142399227:
            print(f"University Drive deck elevation: cell {base}")
    return surface, bridges


def buildings(layers, osm, ground, surface):
    style = np.zeros((EDGE,EDGE),dtype=np.uint8)
    top = np.zeros_like(style)
    by_id = {i:(t,p) for i,t,p in osm}
    city_polys = [polygon(f["geometry"]["rings"]) for f in layers[3]]
    assignments = {}
    for name,lat,lon,kind,height,ident in LANDMARKS + SECONDARY:
        if not kind:
            continue
        p = Point(xy(lat,lon))
        if ident and ident in by_id:
            outline = make_valid(Polygon(by_id[ident][1]))
            matching = [i for i,g in enumerate(city_polys)
                        if g.area >= 25 and g.intersects(outline) and g.intersection(outline).area/g.area > .43]
        else:
            matching = []
        if not matching:
            matching = [min(range(len(city_polys)),key=lambda i: city_polys[i].distance(p))]
        for index in matching:
            assignments[index] = (kind,height)
        print(f"{name:28} {cell(lat,lon)} | {len(matching)} surveyed polygons")
    for index,g in enumerate(city_polys):
        if g.area < 16:
            continue
        rect, pixels = mask(g)
        if not np.any(pixels):
            continue
        guess = (TOWN,3 if g.area < 320 else 4) if g.area < 950 else (STONE,6) if g.area < 4000 else (MODERN,8)
        material, height = assignments.get(index,guess)
        terrain = ground[rect]
        base = int(np.median(terrain[pixels]))
        depth = distance_transform_edt(np.pad(pixels,1))[1:-1,1:-1]
        rise = np.zeros_like(depth,dtype=np.int8)
        if material in [TOWN,UC,MIDDLESEX,GALLERY,MEDWAY]:
            roof_rise = 3 if material in [UC,MIDDLESEX] else 2
            if material != TOWN or g.area < 600:
                rise = np.clip((depth/1.7).astype(np.int8),0,roof_rise)
        if material == WELDON:
            rise = np.where(depth > 2,0,-2)
        roof = np.maximum(terrain+2, base + height + rise)
        overwrite = pixels & ((top[rect] < roof) | (style[rect] == 0))
        top[rect][overwrite] = roof[overwrite]
        style[rect][overwrite] = material
    # Survey data can predate recently added campus wings. Use mapped outlines for
    # significant buildings where the city layer has no coverage.
    for _, tags, points in osm:
        if tags.get("building") not in {"university","college","dormitory"} or len(points) < 4 or points[0] != points[-1]:
            continue
        rect,pixels = mask(make_valid(Polygon(points)))
        if not np.any(pixels) or np.mean(style[rect][pixels] != 0) >= .65:
            continue
        s,t = style[rect],top[rect]
        pixels &= s == 0
        height = int(tags["building:levels"])*2 if tags.get("building:levels","").isdigit() else 7
        s[pixels] = MODERN
        t[pixels] = np.clip(ground[rect][pixels]+height,0,58)
    return style, top


def trees(layers, ground, surface, style):
    rng = np.random.default_rng(19)
    result = {}
    def add(x,z):
        if not (2 <= x < EDGE-2 and 2 <= z < EDGE-2):
            return
        if style[z,x] or surface[z,x] not in [GRASS,WOODS]:
            return
        if any((x+i,z+j) in result for i in range(-2,3) for j in range(-2,3)):
            return
        result[x,z] = (int(ground[z,x]), int(rng.integers(0,6)))
    for feature in layers[45]:
        point = feature["geometry"]
        add(int((point["x"]-WEST)/CELL),int((NORTH-point["y"])/CELL))
    for z in range(4,EDGE-4,4):
        for x in range(4,EDGE-4,4):
            xx,zz = x+int(rng.integers(-2,3)),z+int(rng.integers(-2,3))
            if surface[zz,xx] == WOODS:
                add(xx,zz)
    return result


def write_header(path, ground, surface, style, top, plants, bridges):
    cells = (ground.astype(np.uint32) | (surface.astype(np.uint32)<<6)
             | (top.astype(np.uint32)<<10) | (style.astype(np.uint32)<<16)).ravel()
    cuts = np.r_[0, np.flatnonzero(cells[:-1]!=cells[1:])+1, cells.size]
    data = []
    for start,end in zip(cuts[:-1],cuts[1:]):
        while start < end:
            count = min(1024,end-start)
            data.append(int(cells[start]) | ((count-1)<<21))
            start += count
    points = [x | (z<<10) | (y<<20) | (kind<<26) for (x,z),(y,kind) in sorted(plants.items())]
    decks = [int(x) | (int(z)<<10) | (int(bridges[z,x])<<20) for z,x in zip(*np.nonzero(bridges))]
    def array(values):
        return "\n".join("    "+", ".join(f"0x{x:08x}" for x in values[i:i+8])+"," for i in range(0,len(values),8))
    def position(lat,lon):
        x,z = cell(lat,lon)
        return f"{{{x},{z}}}"
    def labels(rows):
        return "\n".join(f'    {{"{name}",{position(lat,lon)}}},' for name,lat,lon,*_ in rows)
    path.with_name("campus_landmarks.hpp").write_text(f"""// Generated by tools/bake_campus.py.
#pragma once
#include <glm/vec2.hpp>

namespace wsim {{

struct Landmark {{ const char* name; glm::ivec2 position; }};
inline constexpr Landmark landmarks[] = {{
{labels(LANDMARKS)}
}};
inline constexpr Landmark mapLabels[] = {{
{labels(MAP_LABELS)}
}};
inline constexpr glm::ivec2 campusSpawn{position(*SPAWN)};
inline constexpr glm::ivec2 memorialTower{position(*MEMORIAL_TOWER)};
inline constexpr glm::ivec2 middlesexTower{position(*CLOCK_TOWER)};
inline constexpr int surveyEdge = {EDGE};

}} // namespace wsim
""")
    path.write_text(f"""// Generated by tools/bake_campus.py from City of London and OpenStreetMap data.
#pragma once
#include <cstdint>

namespace wsim::survey {{

inline constexpr std::uint32_t columns[] = {{
{array(data)}
}};
inline constexpr std::uint32_t bridges[] = {{
{array(decks)}
}};
inline constexpr std::uint32_t trees[] = {{
{array(points)}
}};

}} // namespace wsim::survey
""")
    print(f"{len(data):,} terrain runs, {np.count_nonzero(style):,} building cells, {len(points):,} trees")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache",type=Path)
    parser.add_argument("--preview",type=Path)
    args = parser.parse_args()
    args.cache.mkdir(parents=True,exist_ok=True)
    ids = [3,5,11,30,34,37,38,43,45,47,49]
    with ThreadPoolExecutor(max_workers=5) as workers:
        future = workers.submit(lambda:list(osm_data(args.cache)))
        layers = dict(zip(ids,workers.map(lambda layer:city(args.cache,layer),ids)))
        osm = future.result()
    print("Interpolating surveyed elevations")
    ground = elevation(layers)
    print("Rasterizing streets, paths, and the Thames")
    surface, bridges = surfaces(layers,osm,ground)
    style,top = buildings(layers,osm,ground,surface)
    plants = trees(layers,ground,surface,style)
    if args.preview:
        ground_color = np.array([[129,155,91],[64,113,143],[157,127,84],[108,108,115],
                                 [200,185,157],[209,192,165],[128,127,129],[203,204,189],
                                 [114,150,72],[143,132,119],[95,129,71]])
        rgb = ground_color[surface]
        rgb[bridges != 0] = ground_color[BRIDGE]
        rgb[style>0] = [136,111,107]
        rgb[np.isin(style,[UC,MIDDLESEX,WELDON,ONTARIO])] = [166,128,102]
        rgb = np.clip(rgb+(ground.astype(int)[:,:,None]-14)*3,0,255).astype(np.uint8)
        for x,z in plants:
            rgb[z,x] = [69,117,73]
        Image.fromarray(rgb).save(args.preview)
    for name,lat,lon,*_ in LANDMARKS+MAP_LABELS+[("Spawn",*SPAWN),
                    ("Memorial Tower",*MEMORIAL_TOWER),("Middlesex clock",*CLOCK_TOWER)]:
        x,z = cell(lat,lon)
        print(name,x,z,"terrain",ground[z,x],"surface",surface[z,x],"building",style[z,x],"top",top[z,x])
    write_header(Path(__file__).resolve().parents[1]/"src/campus_survey.hpp",ground,surface,style,top,plants,bridges)


if __name__ == "__main__":
    main()
