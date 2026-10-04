import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('observe',Path(__file__).parents[1]/'tools/observe.py')
observe=importlib.util.module_from_spec(spec); spec.loader.exec_module(observe)

class Clock:
    def __init__(self): self.t=0
    def now(self): return self.t
    def sleep(self,v): self.t+=v

class TraceTests(unittest.TestCase):
    def test_session_and_sequence(self):
        c=observe.Continuity()
        self.assertFalse(c.check({'sessionId':'a','loadGeneration':1,'sampleId':1}))
        self.assertTrue(c.check({'sessionId':'a','loadGeneration':2,'sampleId':2}))
        with self.assertRaises(RuntimeError): c.check({'sessionId':'b','loadGeneration':2,'sampleId':3})
        with self.assertRaises(RuntimeError): c.check({'sessionId':'a','loadGeneration':2,'sampleId':2})
    def test_missed_intervals_are_explicit_and_no_overwrite(self):
        timer=Clock(); seq=0
        def fetch(port,query):
            nonlocal seq
            seq+=1; timer.t+=.35
            return {'sessionId':'a','loadGeneration':1,'sampleId':seq}
        with tempfile.TemporaryDirectory() as d:
            target=Path(d)/'trace.jsonl'
            observe.record(8921,{},target,1,.1,fetch=fetch,clock=timer.now,sleep=timer.sleep)
            lines=[json.loads(v) for v in target.read_text().splitlines()]
            self.assertTrue(lines[0]['unobservedBetweenSamples'])
            self.assertTrue(any(v.get('missedIntervals',0)>0 for v in lines))
            with self.assertRaises(FileExistsError): observe.record(8921,{},target,1,.1)
    def test_error_is_recorded_and_size_is_bounded(self):
        timer=Clock()
        def fail(*args): raise RuntimeError('fixture unavailable')
        with tempfile.TemporaryDirectory() as d:
            target=Path(d)/'trace.jsonl'
            with self.assertRaisesRegex(RuntimeError,'fixture unavailable'):
                observe.record(1,{},target,1,.1,fetch=fail,clock=timer.now,sleep=timer.sleep)
            self.assertEqual(json.loads(target.read_text().splitlines()[-1])['kind'],'error')
            with self.assertRaises(RuntimeError): observe.record(1,{},Path(d)/'tiny.jsonl',1,.1,max_bytes=1)

if __name__=='__main__': unittest.main()
