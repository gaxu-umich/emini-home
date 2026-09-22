#!/usr/bin/env python3
"""Offline parser/PNG/config/renderer checks, with ASan and UBSan.

Requires a C compiler, zlib headers, Python 3 and Node. Optional real API inputs:
  python3 tools/test_pokemon.py --species /tmp/pokemon-species-25.json \
      --sprite /tmp/pokemon-25.png --output-dir /tmp/pokemon-preview
The host adapter substitutes zlib for the ESP32 ROM's miniz inflate routine;
PNG parsing, filtering, quantization and the production renderer run unchanged.
"""
import argparse
import datetime
import math
import json
from pathlib import Path
import os
import struct
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]

def chunk(tag, data):
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data))

def png(w, h, depth, kind, rows, extra=b''):
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, kind, 0, 0, 0))
            + extra + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

def run(*args, cwd=None):
    subprocess.run([str(x) for x in args], cwd=cwd, check=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--species', type=Path)
    parser.add_argument('--sprite', type=Path)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='home-pokemon-') as temp:
        work = Path(temp)
        out = args.output_dir.resolve() if args.output_dir else work
        out.mkdir(parents=True, exist_ok=True)
        (work/'miniz.h').write_text('''#include <zlib.h>
#include <stddef.h>
#define TINFL_FLAG_PARSE_ZLIB_HEADER 1
static size_t tinfl_decompress_mem_to_mem(void *o,size_t n,const void *i,size_t m,int flags) {
    (void)flags; uLongf size=n;
    return uncompress(o,&size,i,m)==Z_OK ? (size_t)size : (size_t)-1;
}
''')
        sources = ['tests/pokemon_test.c', 'tests/screens_test.c', 'firmware/main/home_pokemon.c',
                   'firmware/main/home_config.c', 'firmware/main/home_places.c',
                   'firmware/main/home_render.c', 'firmware/main/home_parse_air.c', 'firmware/main/home_parse.c',
                   'firmware/main/home_sky.c', 'firmware/main/generated/home_font.c',
                   'firmware/main/generated/home_zones.c',
                   'firmware/components/home_json/cJSON.c',
                   'firmware/components/home_qr/home_qr.c', 'firmware/components/home_qr/qrcodegen.c']
        exe = work/'pokemon-test'
        run(os.environ.get('CC','cc'), '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-DCJSON_NESTING_LIMIT=16', '-g',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
            '-I'+str(work), '-I'+str(ROOT/'firmware/main'),
            '-I'+str(ROOT/'firmware/components/home_json'),
            '-I'+str(ROOT/'firmware/components/home_qr'),
            *(ROOT/x for x in sources), '-lz', '-lm', '-o', exe)
        run(exe, 'settings', cwd=work)
        begin=datetime.datetime(2026,9,22,tzinfo=datetime.timezone.utc)
        series=[]
        icons=['clearsky_day','cloudy','partlycloudy_day','rain','clearsky_day','snow','fair_day','cloudy']
        for hour in list(range(48))+list(range(48,193,6)):
            at=begin+datetime.timedelta(hours=hour)
            details={'air_temperature':round(12+hour//24+5*math.sin((hour%24-6)*math.pi/12),1),
                     'wind_speed':3,'cloud_area_fraction':25}
            period='next_1_hours' if hour<48 else 'next_6_hours'
            series.append({'time':at.isoformat().replace('+00:00','Z'),'data':{
                'instant':{'details':details},period:{'summary':{'symbol_code':icons[min(hour//24,7)]},
                'details':{'precipitation_amount':0}}}})
        weather=work/'weather.json'
        weather.write_text(json.dumps({'properties':{'meta':{'updated_at':begin.isoformat().replace('+00:00','Z')},'timeseries':series}}))
        run(exe,'screens',weather,out)
        run('node', '-e', '''const assert=require('node:assert/strict');
const C=require(process.argv[1]), c=require(process.argv[2]);
assert.deepEqual(C.validate(c), []);
assert.equal(C.screens[5], 'pokemon');
assert.notEqual(C.sourcesKey({sources:{pokemon:{valid:false}}}),
                C.sourcesKey({sources:{pokemon:{valid:true,fetched_at:1}}}));
for(const count of [3,5]) {
 const old=C.clone(c); old.enabled=old.enabled.slice(0,count);old.order=old.order.slice(0,count);
 for(const key of C.screens.slice(count)) delete old.styles[key];
 old.fixed_screen='weather'; assert.deepEqual(C.validate(old),[]);
}
''', ROOT/'firmware/ui/core.js', work/'config.json')
        def check(name, data, valid=True):
            path=work/(name+'.png'); path.write_bytes(data)
            packed=work/(name+'.packed')
            run(exe,'png',path,int(valid),packed)
            return packed.read_bytes() if valid else b''
        # All PNG row predictors must reconstruct identical RGBA pixels.
        raw = bytes([255,210,0,255,190,0,0,255,0,0,0,255,255,255,255,0])*3
        baseline=None
        for filt in range(5):
            encoded=bytearray()
            for y in range(3):
                encoded.append(filt)
                for x in range(16):
                    a=raw[y*16+x-4] if x>=4 else 0
                    b=raw[(y-1)*16+x] if y else 0
                    c=raw[(y-1)*16+x-4] if y and x>=4 else 0
                    p=a+b-c
                    predictor=[a,b,c][min(range(3),key=lambda k:abs(p-[a,b,c][k]))]
                    pred=[0,a,b,(a+b)//2,predictor][filt]
                    encoded.append((raw[y*16+x]-pred)%256)
            packed=check('filter'+str(filt),png(4,3,8,6,encoded))
            if baseline is None: baseline=packed
            assert packed==baseline
            for x,expected in enumerate((2,3,0,0)):
                at=46*96+46+x
                assert (packed[at//4] >> (6-2*(at%4))) & 3 == expected
        # Flat fills preserve warm shading; cool bodies remain white with a black contour.
        rows = b'\0' + bytes([0,0,0,0, 30,140,240,255, 0,0,0,0])
        packed = check('outline', png(3,1,8,6,rows))
        def pixel(data, x, y):
            at=y*96+x
            return (data[at//4] >> (6-2*(at%4))) & 3
        assert pixel(packed,47,47)==1
        for x,y in ((46,47),(48,47),(47,46),(47,48)):
            assert pixel(packed,x,y)==0
        assert pixel(packed,46,46)==1  # no recursive outline growth
        flat = bytes([240,200,20,255, 155,110,10,255, 220,40,20,255, 20,20,20,255])
        packed = check('flat-fill',png(4,1,8,6,b'\0'+flat))
        assert [pixel(packed,46+x,47) for x in range(4)] == [2,2,3,0]
        # Indexed sprites, including the 4-bit PNG format used by PokéAPI.
        extra=chunk(b'PLTE',bytes([0,0,0,255,210,0]))+chunk(b'tRNS',bytes([0,255]))
        for depth in (1,2,4,8):
            check('indexed'+str(depth),png(1,1,depth,3,b'\0'+bytes([1<<(8-depth)]),extra))
        for kind,channels in ((0,1),(2,3),(4,2)):
            check('color'+str(kind),png(1,1,8,kind,b'\0'+bytes([128]*channels)))
        valid=png(1,1,8,6,b'\0\xff\xff\0\xff')
        for end in range(len(valid)):
            check('truncated',valid[:end],False)
        damaged=bytearray(valid);damaged[-1]^=1;check('crc',damaged,False)
        check('oversize',png(97,1,8,6,b'\0'+bytes(97*4)),False)
        check('badfilter',png(1,1,8,6,b'\5\0\0\0\0'),False)
        check('shortinflate',png(1,1,8,6,b'\0\0'),False)
        check('longinflate',png(1,1,8,6,b'\0'+bytes(8)),False)
        check('palette-index',png(1,1,8,3,b'\0\2',extra),False)
        fixture=work/'species.json'
        fixture.write_text(json.dumps({'id':25,'names':[{'name':'Pikachu','language':{'name':'en'}}],
            'genera':[{'genus':'Mouse Pokémon','language':{'name':'en'}}],
            'flavor_text_entries':[{'flavor_text':'A friendly\nPokémon.\fElectric sparks!',
                                    'language':{'name':'en'}}]}))
        sprite=work/'sprite.png';sprite.write_bytes(valid)
        run(exe,'card',args.species.resolve() if args.species else fixture,
            args.sprite.resolve() if args.sprite else sprite,out)
        for frame in out.glob('*.frame'):
            palette=((26,26,22),(230,229,219),(247,173,1),(123,0,1))
            pixels=b''.join(bytes(palette[(v>>shift)&3]) for v in frame.read_bytes() for shift in (6,4,2,0))
            rows=b''.join(b'\0'+pixels[y*1200:(y+1)*1200] for y in range(300))
            frame.with_suffix('.png').write_bytes(png(400,300,8,2,rows))
        print('PASS: config migration, scheduling, UI validation, JSON parsing, PNG filters/bounds/CRC and rendering')
        if args.output_dir: print('Previews:',out)

if __name__ == '__main__':
    main()
