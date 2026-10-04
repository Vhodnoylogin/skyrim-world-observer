"""Fetch pinned API outside Git and build with a clean or verified external cache."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]

def outside(path):
    path = Path(path).resolve()
    if path == ROOT or ROOT in path.parents:
        raise ValueError('Build/dependency directory must be outside this repository')
    return path

def cmake_path():
    if found := shutil.which('cmake'):
        return found
    vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    found = Path(vs) / 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    if not found.is_file():
        raise RuntimeError('Install CMake or Visual Studio CMake component')
    return str(found)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=outside, required=True)
    parser.add_argument('--dependency-cache', type=outside)
    parser.add_argument('--contracts-only', action='store_true')
    args = parser.parse_args()
    deps = json.loads((ROOT/'dependencies.json').read_text())
    build = args.build_dir
    build.mkdir(parents=True, exist_ok=True)
    api_dir = build / 'external/devbench-api'
    api_dir.mkdir(parents=True, exist_ok=True)
    downloaded = {}
    for name, expected in deps['DevBenchAPI']['files'].items():
        url = f"https://raw.githubusercontent.com/alandtse/devbench/{deps['DevBenchAPI']['commit']}/include/{name}"
        target = api_dir/name
        raw = urllib.request.urlopen(url, timeout=30).read()
        digest = hashlib.sha256(raw).hexdigest()
        if digest != expected:
            raise RuntimeError(f'Pinned DevBench ABI hash mismatch: {name}')
        target.write_bytes(raw)
        downloaded[name] = digest
    command = [cmake_path(), '-S', str(ROOT), '-B', str(build), '-A', 'x64']
    cache_pins = {}
    if args.dependency_cache:
        names = {'CommonLibSSE-NG':'commonlibsse', 'fmt':'fmt', 'spdlog':'spdlog', 'nlohmann_json':'nlohmann_json', 'rapidcsv':'rapidcsv'}
        for name, directory in names.items():
            source = args.dependency_cache / (directory+'-src')
            head = subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],text=True).strip()
            if head != deps[name]['commit']:
                raise RuntimeError(f'Cached dependency commit mismatch: {name}')
            cache_pins[name] = head
        command.append('-DOBSERVER_DEPENDENCY_CACHE='+str(args.dependency_cache))
    if args.contracts_only:
        command.append('-DOBSERVER_BUILD_PLUGIN=OFF')
    subprocess.run(command, check=True)
    subprocess.run([cmake_path(), '--build',str(build),'--config','Release','--parallel','4'], check=True)
    subprocess.run([str(Path(cmake_path()).with_name('ctest.exe')),'--test-dir',str(build),'-C','Release','--output-on-failure'],check=True)
    manifest = {'schemaVersion':1, 'devbenchApi':downloaded, 'cacheCommits':cache_pins,
                'sourceFiles':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((ROOT/'src').glob('*'))}}
    dll=build/'Release/SkyrimWorldObserver.dll'
    if dll.exists():
        manifest['dllSha256']=hashlib.sha256(dll.read_bytes()).hexdigest()
    (build/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(manifest,indent=2))

if __name__=='__main__':
    main()
