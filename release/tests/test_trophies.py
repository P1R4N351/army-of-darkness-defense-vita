"""Host clean tests; optional AODD_ASSETS points at user's immutable ORIGINAL assets.
Optional lupa.lua51 executes the original predicates, not a reimplementation.
"""
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import sys
import unittest
import xml.etree.ElementTree as ET
import zlib

RELEASE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(RELEASE/'offline'))
sys.path.insert(0, str(RELEASE/'trophies'))
import aod_trophies as patch
import aod_offline as offline
import build_pack as pack

ASSETS = os.environ.get('AODD_ASSETS')
CORE = os.environ.get('AODD_TROPHY_CORE')
try:
    from lupa.lua51 import LuaRuntime
except ImportError:
    LuaRuntime = None

class Pack(unittest.TestCase):
    def check_png(self, data, want):
        self.assertEqual(data[:8],b'\x89PNG\r\n\x1a\n')
        pos=8; payload=b''; dims=None
        while pos<len(data):
            n,=struct.unpack_from('>I',data,pos)
            tag=data[pos+4:pos+8]; body=data[pos+8:pos+8+n]
            crc,=struct.unpack_from('>I',data,pos+8+n)
            self.assertEqual(crc,zlib.crc32(tag+body)&0xffffffff)
            if tag==b'IHDR': dims=struct.unpack_from('>II',body)
            if tag==b'IDAT': payload+=body
            pos+=12+n
        self.assertEqual(dims,want)
        self.assertEqual(len(zlib.decompress(payload)),(want[0]*4+1)*want[1])
    def test_pack_independent_reader(self):
        b = pack.build()
        self.assertEqual(b, pack.build())
        magic, version, size, count, entry, zero = struct.unpack_from('>IIQIII', b)
        self.assertEqual((magic,version,size,count,entry,zero),(0xdca24d00,2,len(b),55,64,0))
        check = bytearray(b); check[28:48]=b'\0'*20
        self.assertEqual(hashlib.sha1(check).digest(),b[28:48])
        files={}; end=64+count*64
        for i in range(count):
            name, offset, length, flags = struct.unpack_from('>32sQQI',b,64+i*64)
            name=name.rstrip(b'\0').decode()
            self.assertNotIn(name,files)
            self.assertGreaterEqual(offset,end)
            self.assertEqual(offset%16,0)
            self.assertEqual(flags,0)
            self.assertLessEqual(offset+length,len(b))
            files[name]=b[offset:offset+length]; end=offset+length
        self.assertEqual(list(files)[:2],['TROPCONF.SFM','TROP.SFM'])
        self.assertEqual(end,len(b))
        for name in ['TROPCONF.SFM','TROP.SFM']:
            self.assertTrue(files[name].startswith(b'<!--Sce-Np-Trophy-Signature: '+b'x'*320+b'-->'))
            tree=ET.fromstring(files[name])
            self.assertEqual(tree.findtext('npcommid'),'AODD00001_00')
            trophies=tree.findall('trophy')
            self.assertEqual([int(t.attrib['id']) for t in trophies],list(range(52)))
            self.assertTrue(all(t.attrib['ttype']=='B' and t.attrib['pid']=='-1' for t in trophies))
            if name == 'TROP.SFM':
                for trophy, mapping in zip(trophies, pack.mapping()['trophies']):
                    label=pack.metadata()[mapping['key']]
                    self.assertEqual(trophy.findtext('name'),label['name'])
                    self.assertEqual(trophy.findtext('detail'),label['detail'])
                    self.assertNotIn('Original achievement',label['name'])
                    self.assertNotIn('ach_',label['detail'])
        for name,data in files.items():
            if not name.endswith('.PNG'): continue
            want=(320,176) if name=='ICON0.PNG' else (240,240)
            self.check_png(data,want)
    def test_frozen_mapping(self):
        data=pack.mapping()
        self.assertEqual(set(pack.metadata()),{t['key'] for t in data['trophies']})
        self.assertEqual(hashlib.sha256((RELEASE/'trophies/mapping.json').read_bytes()).hexdigest(),
                         'c4005f360615cc16024b0e1e7cb49f0a076dcdbb0ae4b7e0d427aff1ece7ec94')
    @unittest.skipUnless((RELEASE.parent/'source/aod/trophy_keys.inc').is_file(),'native source tree only')
    def test_native_table(self):
        inc=(RELEASE.parent/'source/aod/trophy_keys.inc').read_text()
        self.assertEqual(re.findall(r'"(ach_[a-z0-9_]+)"',inc),[t['key'] for t in pack.mapping()['trophies']])

@unittest.skipUnless(ASSETS,'requires original own-APK assets')
class PrivatePatch(unittest.TestCase):
    def setUp(self):
        self.original=(Path(ASSETS)/patch.REL).read_bytes()
        self.assertEqual(hashlib.sha256(self.original).hexdigest(),offline.INPUT_SHA256[patch.REL])
        self.core=self.original
        for op in offline.LUA_CORE[patch.REL]:
            self.assertEqual(op[0],'sub')
            self.core=self.core.replace(op[1].encode(),op[2].encode())
        self.modified=patch.transform(self.core)
    def test_strict_and_idempotent(self):
        self.assertEqual(patch.transform(self.modified),self.modified)
        for bad in [self.original,self.core+b' ',self.modified+b' ',b'',self.core.replace(b'100.0',b'99.0')]:
            with self.assertRaises(ValueError): patch.transform(bad)
        # No original predicate text is changed, only central dispatch.
        self.assertEqual(self.modified.replace(patch.NEW.encode(),patch.OLD.encode()),self.core)
    def test_original_mapping_exact(self):
        j=json.loads((Path(ASSETS)/'serviceconfigs/gamecenter.json').read_bytes()[32:])
        self.assertEqual({t['key'] for t in pack.mapping()['trophies']},
                         {t['key'] for t in j['services'][0]['params']['achievements']})
        self.assertEqual(set(re.findall(rb'"(ach_[a-z0-9_]+)"',self.original)),
                         {t['key'].encode() for t in pack.mapping()['trophies']})
    @unittest.skipUnless(LuaRuntime and CORE,'requires host Lua 5.1 and compiled native core')
    def test_original_predicates_to_native_bridge(self):
        lib=ctypes.CDLL(CORE)
        lib.aod_trophy_message.argtypes=[ctypes.c_char_p];lib.aod_trophy_message.restype=ctypes.c_int
        def runtime(source,modified):
            lua=LuaRuntime(unpack_returned_tuples=True)
            events=[]
            lua.globals().report=lambda key,progress: events.append((key,progress))
            lua.globals().native_message=lambda m: events.append((m.split(':',1)[1],100)) if lib.aod_trophy_message(m.encode())>=0 else None
            lua.execute('''
values={}; gold=0; nextlevel=true
engine={instance={chunks={
 util={getNumberValue=function(k,d) return values[k] or d or 0 end,
       setNumberValue=function(k,v) values[k]=v end},
 events={}, load={hasNextLevel=function() return nextlevel end}, globals={final_wave=50}}}}
GameObject={TEAM_ENEMY=2,TEAM_ALLY=1}
GameCenterService={get=function() return {reportCompletion=function(self,p,k,cb) report(k,p) end} end}
UserData={instance=function() return {getGold=function() return gold end} end}
os.getenv=function(m) native_message(m); return nil end
''')
            lua.execute(source.decode('latin1').replace('module(...,package.seeall)','module("ach",package.seeall)',1))
            return lua,events
        orig,oe=runtime(self.original,False);mod,me=runtime(self.modified,True)
        # Replay real predicate entry points on the original and transformed modules.
        # No backfill just from loading the module / setting old counters.
        self.assertEqual(me,[])
        scenarios=[
            'ach.unitKnockedIntoPit("skeleton")',
            'ach.unitKnockedIntoPit("evil_sheila")',
            'gold=4999; ach.goldChanged()',
            'gold=5000; ach.goldChanged()',
            'gold=10000; ach.goldChanged()',
            'gold=50000; ach.goldChanged()',
            'for i=1,24 do ach.magicWordsResult("necktie") end',
            'ach.magicWordsResult("necktie")',
            'ach.magicWordsResult("wrong")',
            'ach.typeUpgraded("unit","peasant")',
            'nextlevel=false; ach.typeUpgraded("unit","peasant")',
            'ach.ironCapUpgraded(1,2)',
            'ach.ironCapUpgraded(2,2)',
            'for i=1,24 do ach.swordBoyUpgradedUnit("peasant") end',
            'ach.swordBoyUpgradedUnit("peasant")',
            'for i=1,25 do ach.torchBoyUpgradedUnit("peasant") end',
            'ach.waveStarted({},1); ach.unitKnockedIntoPit("evil_sheila")',
            'ach.waveStarted({},3); ach.unitKnockedIntoPit("evil_sheila")',
        ]
        for scenario in scenarios:
            with self.subTest(scenario=scenario):
                oe.clear();me.clear();orig.execute(scenario);mod.execute(scenario)
                self.assertEqual(me,[(k,p) for k,p in oe if p==100])
        # Obtain dispatch closure from actual module to exercise boundary values, all IDs.
        mod.execute('''function dispatch(k,p)
 for i=1,20 do local n,f=debug.getupvalue(ach.goldChanged,i)
  if n=="pushAchievementData" then f(k,p); return end
 end
 error("dispatch closure missing") end''')
        me.clear()
        for t in pack.mapping()['trophies']:
            mod.globals().dispatch(t['key'],100)
        self.assertEqual([k for k,p in me],[t['key'] for t in pack.mapping()['trophies']])
        me.clear()
        for progress in [0,1,99.9,101,math.nan,math.inf,-math.inf,'100',None]:
            mod.globals().dispatch('ach_bone_saw',progress)
        mod.globals().dispatch('unknown',100)
        self.assertEqual(me,[])
        mod.execute('os.getenv=nil; ach.unitKnockedIntoPit("evil_sheila")')
        self.assertEqual(me,[]) # missing bridge is a safe no-op

if __name__=='__main__': unittest.main()
