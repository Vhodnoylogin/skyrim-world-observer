"""Read observer snapshots or record a bounded loss-aware external JSONL trace."""
import argparse
import json
import math
from pathlib import Path
import time
import urllib.request

ROOT=Path(__file__).resolve().parents[1]

def request(port, query):
    raw=json.dumps(query,allow_nan=False).encode()
    req=urllib.request.Request(f'http://127.0.0.1:{port}/api/tool/inspect', data=raw,headers={'Content-Type':'application/json'})
    with urllib.request.urlopen(req,timeout=5) as response:
        result=json.load(response)
    if result.get('ok') is not True or result.get('schemaVersion')!=1 or not result.get('sessionId'):
        raise RuntimeError('Observer unavailable/rejected: '+json.dumps(result))
    return result

class Continuity:
    def __init__(self):
        self.session=None
        self.generation=None
        self.sample=0
    def check(self,value):
        session=value['sessionId']
        if self.session is not None and session!=self.session:
            raise RuntimeError('Game session changed during trace')
        generation=value['loadGeneration']
        sample=value['sampleId']
        if isinstance(sample,bool) or not isinstance(sample,int) or sample<=self.sample:
            raise RuntimeError('Non-increasing observer sample sequence')
        boundary=self.generation is not None and generation!=self.generation
        self.session,self.generation,self.sample=session,generation,sample
        return boundary

def record(port,query,output,duration,interval,max_bytes=16*1024*1024,fetch=request,clock=time.monotonic,sleep=time.sleep):
    path=Path(output).resolve()
    if path==ROOT or ROOT in path.parents:
        raise ValueError('Trace files belong outside this repository')
    if not all(math.isfinite(v) for v in (duration,interval)) or not 0<duration<=600 or not .1<=interval<=10:
        raise ValueError('duration in (0,600], interval in [.1,10]')
    tracker=Continuity()
    started=clock(); deadline=started+duration; due=started; size=0
    with path.open('x',encoding='utf-8') as stream:
        def emit(value):
            nonlocal size
            text=json.dumps(value,allow_nan=False,separators=(',',':'))+'\n'
            size+=len(text.encode())
            if size>max_bytes: raise RuntimeError('Trace size bound exceeded')
            stream.write(text); stream.flush()
        emit({'kind':'header','schemaVersion':1,'sampling':'poll','intervalSeconds':interval,'unobservedBetweenSamples':True})
        while clock()<deadline:
            if clock()<due: sleep(min(due-clock(),deadline-clock()))
            if clock()>=deadline: break
            actual=clock()
            missed=max(0,int((actual-due)/interval))
            try:
                value=fetch(port,query)
                boundary=tracker.check(value)
                emit({'kind':'sample','clientMonotonicSeconds':actual,'missedIntervals':missed,'loadBoundary':boundary,'data':value})
            except Exception as exc:
                emit({'kind':'error','message':str(exc),'clientMonotonicSeconds':clock()})
                raise
            due+=interval*(missed+1)
        emit({'kind':'end','elapsedSeconds':clock()-started})

def main():
    p=argparse.ArgumentParser(description=__doc__)
    group=p.add_mutually_exclusive_group(required=True)
    group.add_argument('--port',type=int)
    group.add_argument('--runtime',type=Path)
    p.add_argument('--query',type=Path,default=ROOT/'examples/player.json')
    p.add_argument('--output',type=Path)
    p.add_argument('--duration',type=float,default=5)
    p.add_argument('--interval',type=float,default=.2)
    args=p.parse_args()
    port=args.port or json.loads(args.runtime.read_text(encoding='utf-8-sig'))['port']
    if not 1<=port<=65535: raise ValueError('Invalid local port')
    query=json.loads(args.query.read_text())
    if args.output: record(port,query,args.output,args.duration,args.interval)
    else: print(json.dumps(request(port,query),indent=2))

if __name__=='__main__':
    main()
