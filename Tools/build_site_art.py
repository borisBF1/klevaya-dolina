# Builds the web site / admin pictures from the game's own art (update 6). Run by Tools/ExportArt.ps1 after the game
# exported Saved/ArtExport (icons x4 + manifest.json, FVIconExport.cpp) and Saved/ArtExport_gif (real gameplay frames).
#   python Tools/build_site_art.py [shots_dir]
# shots_dir = an autotest screenshot folder (portrait 450x1000), default Saved/Shots_twin3_450x1000.
# Output: Server/Fishing-game/img/{icons,sprites,shots,loc}/..., img/gameplay.webp|.gif, img/gameplay_poster.webp,
#         img/art.json (read by lib/art.php).
import json
import os
import shutil
import sys
import time

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAVED = os.path.join(ROOT, 'Saved')
EXPORT = os.path.join(SAVED, 'ArtExport')
GIF_DIR = os.path.join(SAVED, 'ArtExport_gif')
SHOTS = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SAVED, 'Shots_twin3_450x1000')
OUT = os.path.join(ROOT, 'Server', 'Fishing-game', 'img')

GALLERY = [
    ('01_main_menu', 'Главное меню'),
    ('17_fishing_minigame', 'Вываживание: держите рыбу в зелёной полосе'),
    ('18_fishing_showcase', 'Улов: размер, качество и цена'),
    ('58_home_yard', 'Свой двор: дом, огород и постройки'),
    ('59_home_interior', 'Уютный дом с мастерскими'),
    ('11_screen_map', 'Карта долины'),
    ('45_screen_wardrobe_full', 'Гардероб: шляпы, одежда, поплавки и питомцы'),
    ('28_location_city', 'Город у моря'),
    ('142_u6_expert_pendulum', 'Экспертная рыбалка на причале'),
    ('120_u6_mill_working', 'Старая мельница'),
    ('102_u6_screen_boat', 'Починка старой лодки'),
    ('21_screen_collection_after_catch', 'Коллекция рыб'),
    ('27_location_volcano', 'Вулканическое озеро'),
    ('64_screen_station_st_kitchen', 'Кухня: готовим из улова'),
]
# location card: (autotest shot, top of a 420x340 crop: the shore, the player and the water; the HUD column cut off)
LOCATION_SHOTS = {'pond': ('22_location_pond', 250), 'river': ('23_location_river', 250),
                  'mountain': ('24_location_mountain', 250), 'ocean': ('25_location_ocean', 250),
                  'swamp': ('26_location_swamp', 250), 'volcano': ('27_location_volcano', 250),
                  'city': ('133_u6_camwalk_city_pier_down', 380), 'cave': ('140_u6_camwalk_cave_planks', 420),
                  'mill': ('121_u6_mill_dam_omut', 330)}


def ensure(d):
    os.makedirs(d, exist_ok=True)
    return d


def rel(p):
    return os.path.relpath(p, OUT).replace('\\', '/')


def build_icons(art):
    man_path = os.path.join(EXPORT, 'manifest.json')
    if not os.path.isfile(man_path):
        print('no', man_path, '- run Tools/ExportArt.ps1 first; icons skipped')
        return
    man = json.load(open(man_path, encoding='utf-8'))
    dst = ensure(os.path.join(OUT, 'icons'))
    keep = set()
    icons, fish = {}, {}
    for it in man['icons']:
        i = it['id']
        src = os.path.join(EXPORT, 'icons', i + '.png')
        if not os.path.isfile(src):
            continue
        im = Image.open(src).convert('RGBA')
        im.save(os.path.join(dst, i + '.png'), optimize=True)
        keep.add(i + '.png')
        icons[i] = [it['w'], it['h'], it['kind'], it['name']]
        if it['kind'] == 'fish':
            fish[i] = [it.get('rarity', 'common'), 1 if it.get('expert') else 0, it.get('desc', '')]
    for f in os.listdir(dst):
        if f.endswith('.png') and f not in keep:
            os.remove(os.path.join(dst, f))
    art['icons'] = icons
    art['fish'] = fish
    art['locations'] = {l['id']: {'name': l['name'], 'desc': l['desc'], 'min_level': l['min_level']} for l in man['locations']}
    sd = ensure(os.path.join(OUT, 'sprites'))
    art['sprites'] = []
    for f in sorted(os.listdir(os.path.join(EXPORT, 'sprites'))):
        Image.open(os.path.join(EXPORT, 'sprites', f)).save(os.path.join(sd, f), optimize=True)
        art['sprites'].append(f[:-4])
    print('icons:', len(icons), 'sprites:', len(art['sprites']))


def build_gameplay(art):
    log = os.path.join(EXPORT, 'gif_frames.txt')
    if not os.path.isfile(log):
        print('no gameplay frames - gameplay clip skipped')
        return
    rows = [l.split() for l in open(log, encoding='utf-8') if l.strip()]
    frames, times = [], []
    for name, state, cast, t in rows:
        p = os.path.join(GIF_DIR, name + '.png')
        if os.path.isfile(p):
            frames.append((Image.open(p).convert('RGB'), state))
            times.append(float(t))
    if len(frames) < 5:
        print('too few gameplay frames')
        return
    # only the last cast (a missed bite or an escape re-casts)
    last = max(i for i, (_, s) in enumerate(frames) if s == 'charging')
    while last > 0 and frames[last - 1][1] == 'charging':
        last -= 1
    frames, times = frames[last:], times[last:]
    dur = []
    for i in range(len(frames)):
        d = (times[i + 1] - times[i]) * 1000 if i + 1 < len(frames) else 1600
        dur.append(int(min(1600, max(70, d))))
    dur[-1] = 1600
    W, H = frames[0][0].size
    big = [f.resize((360, int(H * 360 / W)), Image.LANCZOS) for f, _ in frames]
    webp = os.path.join(OUT, 'gameplay.webp')
    big[0].save(webp, save_all=True, append_images=big[1:], duration=dur, loop=0, quality=55, method=6,
                allow_mixed=True, minimize_size=True)   # ~600 KB for ~9 s
    small = [f.resize((240, int(H * 240 / W)), Image.LANCZOS) for f, _ in frames]
    mosaic = Image.new('RGB', (240 * 4, small[0].height))
    for k, idx in enumerate((0, len(small) // 3, 2 * len(small) // 3, len(small) - 1)):
        mosaic.paste(small[idx], (240 * k, 0))
    pal = mosaic.quantize(colors=128, method=Image.MEDIANCUT)
    gq = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in small]
    gif = os.path.join(OUT, 'gameplay.gif')
    gq[0].save(gif, save_all=True, append_images=gq[1:], duration=dur, loop=0, optimize=True, disposal=1)
    poster_i = next((i for i, (_, s) in enumerate(frames) if s == 'minigame'), 0) + 8
    poster = os.path.join(OUT, 'gameplay_poster.webp')
    big[min(poster_i, len(big) - 1)].save(poster, quality=78, method=6)
    art['gameplay'] = {'webp': rel(webp), 'gif': rel(gif), 'poster': rel(poster), 'w': big[0].width, 'h': big[0].height,
                       'frames': len(big), 'seconds': round(sum(dur) / 1000, 1)}
    print('gameplay: %d frames, webp %d KB, gif %d KB' % (len(big), os.path.getsize(webp) // 1024, os.path.getsize(gif) // 1024))


def build_shots(art):
    if not os.path.isdir(SHOTS):
        print('no screenshot folder', SHOTS)
        return
    sd = ensure(os.path.join(OUT, 'shots'))
    for f in os.listdir(sd):
        os.remove(os.path.join(sd, f))
    shots = []
    for name, caption in GALLERY:
        p = os.path.join(SHOTS, name + '.png')
        if not os.path.isfile(p):
            print('missing shot', name)
            continue
        im = Image.open(p).convert('RGB')
        key = name.split('_', 1)[1]
        full = os.path.join(sd, key + '.webp')
        im.save(full, quality=80, method=6)
        th = im.resize((240, int(im.height * 240 / im.width)), Image.LANCZOS)
        thumb = os.path.join(sd, key + '_s.webp')
        th.save(thumb, quality=78, method=6)
        shots.append({'img': rel(full), 'thumb': rel(thumb), 'caption': caption, 'w': im.width, 'h': im.height,
                      'tw': th.width, 'th': th.height})
    art['shots'] = shots
    ld = ensure(os.path.join(OUT, 'loc'))
    for loc, (name, top) in LOCATION_SHOTS.items():
        p = os.path.join(SHOTS, name + '.png')
        if not os.path.isfile(p):
            continue
        im = Image.open(p).convert('RGB').crop((0, top, 420, top + 340))
        out = os.path.join(ld, loc + '.webp')
        im.save(out, quality=78, method=6)
        art.setdefault('locations', {}).setdefault(loc, {})
        art['locations'][loc].update({'img': rel(out), 'w': im.width, 'h': im.height})
    print('shots:', len(shots), 'location cards:', len(os.listdir(ld)))


def main():
    ensure(OUT)
    art = {}
    old = os.path.join(OUT, 'art.json')
    if os.path.isfile(old):
        try:
            art = json.load(open(old, encoding='utf-8'))
        except ValueError:
            art = {}
    build_icons(art)
    build_gameplay(art)
    build_shots(art)
    art['built'] = time.strftime('%Y-%m-%d %H:%M')
    with open(old, 'w', encoding='utf-8') as f:
        json.dump(art, f, ensure_ascii=False, separators=(',', ':'))
    total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(OUT) for f in fs)
    print('img/ total %.1f MB -> %s' % (total / 1048576, OUT))


if __name__ == '__main__':
    main()
