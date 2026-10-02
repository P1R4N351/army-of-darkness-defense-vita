"""Independent on-disk journal and link-time I/O fault tests; no Vita SDK."""
import ctypes
import os
from pathlib import Path
import struct
import tempfile
import unittest


def record(generation, earned, done, legacy=False):
    data=(b'AODTRP01'+struct.pack('<QQ',earned,done) if legacy else
          b'AODTRP02'+struct.pack('<QQQ',generation,earned,done))
    checksum=2166136261
    for byte in data:
        checksum=((checksum^byte)*16777619)&0xffffffff
    return data+struct.pack('<Q',checksum)


class Journal(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib=ctypes.CDLL(os.environ['AODD_STORAGE_TEST_LIB'])
        cls.lib.aod_trophy_save_file.argtypes=[ctypes.c_char_p,ctypes.c_uint64,ctypes.c_uint64]
        cls.lib.aod_trophy_save_pending_file.argtypes=[ctypes.c_char_p,ctypes.c_uint64,ctypes.c_uint64,ctypes.c_int]
        cls.lib.aod_trophy_load_file.argtypes=[ctypes.c_char_p,ctypes.POINTER(ctypes.c_uint64),ctypes.POINTER(ctypes.c_uint64)]
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.path=Path(self.tmp.name)/'journal';self.lib.trophy_test_fault(-1,0,0)
    def slot(self,n):return Path(str(self.path)+'.'+str(n))
    def save(self,e,d=0):return self.lib.aod_trophy_save_file(os.fsencode(self.path),e,d)
    def load(self):
        e=ctypes.c_uint64(123);d=ctypes.c_uint64(456)
        rc=self.lib.aod_trophy_load_file(os.fsencode(self.path),ctypes.byref(e),ctypes.byref(d))
        return rc,e.value,d.value
    def test_alternating_slots(self):
        self.assertEqual(self.load(),(1,123,456))
        self.assertEqual(self.save(1),0);self.assertEqual(self.slot(0).read_bytes(),record(1,1,0))
        self.assertEqual(self.save(3,1),0);self.assertEqual(self.slot(1).read_bytes(),record(2,3,1))
        self.assertEqual(self.save(7,3),0);self.assertEqual(self.slot(0).read_bytes(),record(3,7,3))
        self.assertEqual(self.load(),(0,7,3));self.assertFalse(self.path.exists())
    def test_every_interrupted_write_boundary_preserves_current(self):
        self.assertEqual(self.save(1),0);current=self.slot(0).read_bytes()
        for boundary in range(40):
            with self.subTest(boundary=boundary):
                self.lib.trophy_test_fault(boundary,0,0)
                self.assertEqual(self.save(3),-1)
                self.assertEqual(self.slot(0).read_bytes(),current)
                self.assertEqual(self.load(),(0,1,0))
        self.assertEqual(self.save(3),0);self.assertEqual(self.load(),(0,3,0))
    def test_sync_and_close_failure_preserve_previous_slot(self):
        self.assertEqual(self.save(1),0);previous=self.slot(0).read_bytes()
        for sync,close in [(2,0),(0,1)]:
            self.lib.trophy_test_fault(-1,sync,close)
            self.assertEqual(self.save(3),-1)
            self.assertEqual(self.slot(0).read_bytes(),previous)
            # A reported failure may still have produced a valid newer record.
            # Simulate that unsynced record being torn after power loss.
            self.slot(1).write_bytes(self.slot(1).read_bytes()[:17])
            self.assertEqual(self.load(),(0,1,0))
    def test_first_torn_write_recovers_only_with_live_fresh_session(self):
        self.assertEqual(self.load(),(1,123,456))
        for boundary in range(40):
            self.lib.trophy_test_fault(boundary,0,0)
            self.assertEqual(self.lib.aod_trophy_save_pending_file(os.fsencode(self.path),1,0,1),-1)
        self.assertEqual(self.save(1),-1)  # a later startup must not erase unknown corruption
        self.assertEqual(self.lib.aod_trophy_save_pending_file(os.fsencode(self.path),1,0,1),0)
        self.assertEqual(self.load(),(0,1,0))
    def test_unsynced_new_slot_is_synced_before_previous_slot_is_reclaimed(self):
        self.assertEqual(self.save(1),0);previous=self.slot(0).read_bytes()
        self.lib.trophy_test_fault(-1,2,0)  # selected-slot barrier succeeds; new-slot sync fails
        self.assertEqual(self.save(3),-1);self.assertEqual(self.slot(1).read_bytes(),record(2,3,0))
        self.lib.trophy_test_fault(10,1,0)  # next selected-slot barrier fails before peer write
        self.assertEqual(self.save(7),-1);self.assertEqual(self.slot(0).read_bytes(),previous)
        self.slot(1).write_bytes(b'')  # simulated loss of the still-unsynced newer slot
        self.assertEqual(self.load(),(0,1,0))
        self.lib.trophy_test_fault(-1,2,0)
        self.assertEqual(self.save(3),-1)
        self.lib.trophy_test_fault(10,0,0)  # newer record is synced, then peer write is torn
        self.assertEqual(self.save(7),-1)
        self.assertEqual(self.slot(1).read_bytes(),record(2,3,0));self.assertEqual(self.load(),(0,3,0))
    def test_failed_open_does_not_remove_current(self):
        self.assertEqual(self.save(1),0);previous=self.slot(0).read_bytes();self.slot(1).mkdir()
        self.assertEqual(self.save(3),-1)
        self.assertEqual(self.slot(0).read_bytes(),previous);self.assertEqual(self.load(),(0,1,0))
    def test_corrupt_newest_falls_back_and_both_bad_fail_closed(self):
        self.assertEqual(self.save(1),0);self.assertEqual(self.save(3),0)
        for damaged in [b'',b'AODTRP02',record(2,3,0)+b'extra',record(2,3,0)[:-1]+b'X']:
            self.slot(1).write_bytes(damaged);self.assertEqual(self.load(),(0,1,0))
        self.slot(0).write_bytes(b'corrupt')
        self.assertEqual(self.load(),(-1,123,456));self.assertEqual(self.save(7),-1)
    def test_legacy_migration_keeps_original_during_interruption(self):
        old=record(0,3,1,legacy=True);self.path.write_bytes(old)
        self.lib.trophy_test_fault(10,0,0)
        self.assertEqual(self.save(7,3),-1);self.assertEqual(self.load(),(0,3,1))
        self.assertEqual(self.path.read_bytes(),old)
        self.assertEqual(self.save(7,3),0);self.assertEqual(self.load(),(0,7,3))
        self.assertEqual(self.path.read_bytes(),old)
    def test_generation_wrap_and_ambiguity(self):
        self.slot(0).write_bytes(record((1<<64)-1,1,0));self.slot(1).write_bytes(record(0,3,1))
        self.assertEqual(self.load(),(0,3,1));self.assertEqual(self.save(7,3),0)
        self.assertEqual(self.slot(0).read_bytes(),record(1,7,3))
        self.slot(0).write_bytes(record(0,1,0));self.slot(1).write_bytes(record(1<<63,3,1))
        self.assertEqual(self.load(),(-1,123,456));self.assertEqual(self.save(7),-1)
        self.slot(1).write_bytes(record(0,3,1));self.assertEqual(self.load(),(-1,123,456))
        self.slot(1).write_bytes(record(0,1,0));self.assertEqual(self.load(),(0,1,0))
    def test_invalid_masks_and_regression_refused(self):
        self.assertEqual(self.save(3,1),0)
        for earned,done in [(1,0),(0,1),(1<<52,0),(3,4)]:
            self.assertEqual(self.save(earned,done),-1)
        self.assertEqual(self.load(),(0,3,1))
        self.slot(0).write_bytes(record(1,1<<52,0));self.assertEqual(self.load(),(-1,123,456))


if __name__=='__main__':unittest.main(verbosity=2)
