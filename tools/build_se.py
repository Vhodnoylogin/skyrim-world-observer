"""Build the SE104 observer with a reviewed external modern SDK; never install."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('observer_legacy_build', ROOT/'tools/build.py')
legacy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(legacy)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def outside_build(value):
    path = Path(value).resolve()
    if any((p/'.git').exists() for p in (path, *path.parents)):
        raise ValueError('Keep build output outside every Git checkout')
    if path == ROOT or ROOT in path.parents:
        raise ValueError('Build output cannot be inside observer source')
    return path


def pinned_file(pin):
    path = Path(pin['path']).resolve()
    if not path.is_file() or digest(path) != pin.get('sha256'):
        raise ValueError('External SDK file pin mismatch: ' + str(path))
    return path


def provider_headers(build, pins):
    folder = build/'external/provider-api'
    folder.mkdir(parents=True, exist_ok=True)
    result = []
    for name, pin in pins['providerAPIs'].items():
        if Path(name).name != name or name not in ('IPluginInterface.h', 'OBodyAPI.h'):
            raise ValueError('Unexpected provider API header name')
        target = folder/name
        if not target.is_file() or digest(target) != pin['sha256']:
            with urllib.request.urlopen(pin['url'], timeout=30) as response:
                raw = response.read(1024*1024+1)
            if len(raw) > 1024*1024 or hashlib.sha256(raw).hexdigest() != pin['sha256']:
                raise ValueError('Pinned provider ABI download differs: '+name)
            target.write_bytes(raw)
        result.append({'path': str(target), 'sha256': pin['sha256'], 'repository': pin['repository'], 'commit': pin['commit']})
    return folder, result


def header_inventory(directories):
    """Pin all public/header-only dependency inputs, including generated headers."""
    found = {}
    for value in directories:
        folder = Path(value).resolve()
        if not folder.is_dir() or ';' in str(folder):
            raise ValueError('Existing unambiguous include directory required')
        for path in sorted(folder.rglob('*')):
            if path.is_file() and path.suffix.lower() in ('.h', '.hpp', '.hxx', '.inl', '.inc'):
                resolved = path.resolve()
                found[str(resolved)] = digest(resolved)
                if len(found) > 50000:
                    raise ValueError('Unexpectedly large SDK header closure')
    if not found:
        raise ValueError('Actual external SDK headers required')
    return [{'path': p, 'sha256': h} for p, h in sorted(found.items())]


def verify_sdk(sdk, pins):
    if sdk.get('schemaVersion') != 1:
        raise ValueError('SDK manifest schemaVersion1 required')
    for key, dependency in (('source', 'CommonLibSSE-NG'), ('openvr', 'OpenVR')):
        row = sdk[key]
        if row.get('commit') != pins[dependency]['commit']:
            raise ValueError('Wrong modern SDK source identity: ' + key)
        folder = Path(row['path']).resolve()
        actual = subprocess.check_output(['git', '-C', str(folder), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != row['commit']:
            raise ValueError('External SDK checkout differs: ' + key)
        if subprocess.check_output(['git', '-C', str(folder), 'diff', '--name-only', 'HEAD'], text=True).strip():
            raise ValueError('Modified external SDK tracked sources: ' + key)
    source = Path(sdk['source']['path']).resolve()
    if Path(sdk['openvr']['path']).resolve() != source/'extern/openvr':
        raise ValueError('OpenVR must be the exact SDK submodule used by CMake')
    library = pinned_file(sdk['library'])
    if library.name.lower() != 'commonlibsse-ng.lib':
        raise ValueError('Use the actual modern SDK library, never rename a legacy library')
    links = [pinned_file(p) for p in sdk['linkLibraries']]
    if not links or not sdk.get('includeDirs'):
        raise ValueError('Explicit modern link/include closure required')
    includes = [source/'include', source/'extern/openvr/headers', *sdk['includeDirs']]
    return library, links, header_inventory(includes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=outside_build, required=True)
    parser.add_argument('--sdk-manifest', type=Path, required=True)
    args = parser.parse_args()
    pins = json.loads((ROOT/'se-dependencies.json').read_text(encoding='utf-8'))
    manifest_path = args.sdk_manifest.resolve()
    sdk_sha = digest(manifest_path)
    sdk = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    library, links, headers = verify_sdk(sdk, pins)
    build = args.build_dir
    build.mkdir(parents=True, exist_ok=True)
    provider_dir, provider_pins = provider_headers(build, pins)
    api_dir = build/'external/devbench-api'
    api_dir.mkdir(parents=True, exist_ok=True)
    api_pins = []
    api = pins['DevBenchAPI']
    for name, expected in api['files'].items():
        target = api_dir/name
        if not target.is_file() or digest(target) != expected:
            url = f"https://raw.githubusercontent.com/alandtse/devbench/{api['commit']}/include/{name}"
            raw = urllib.request.urlopen(url, timeout=30).read()
            if hashlib.sha256(raw).hexdigest() != expected:
                raise ValueError('Pinned DevBench ABI download differs: ' + name)
            target.write_bytes(raw)
        api_pins.append({'path': str(target), 'sha256': expected})
    source_paths = [ROOT/'CMakeLists.txt', ROOT/'se-dependencies.json', Path(__file__)]
    source_paths += sorted(p for p in (ROOT/'src').glob('*') if p.is_file())
    source_paths += sorted(p for p in (ROOT/'tests').glob('*') if p.is_file())
    source_paths += [ROOT/'tools/build.py']
    source_files = [{'path': str(p), 'sha256': digest(p)} for p in source_paths]
    cmake = legacy.cmake_path()
    command = [cmake, '-S', str(ROOT), '-B', str(build), '-A', 'x64',
               '-DOBSERVER_BUILD_SE104=ON', '-DOBSERVER_SE_SDK_ROOT='+str(Path(sdk['source']['path']).resolve()),
               '-DOBSERVER_SE_SDK_LIBRARY='+str(library),
               '-DOBSERVER_SE_INCLUDE_DIRS='+';'.join(str(Path(p).resolve()) for p in sdk['includeDirs']),
               '-DOBSERVER_SE_LINK_LIBRARIES='+';'.join(str(p) for p in links),
               '-DDEVBENCH_API_DIR='+str(api_dir), '-DOBSERVER_PROVIDER_API_DIR='+str(provider_dir)]
    subprocess.run(command, check=True)
    subprocess.run([cmake, '--build', str(build), '--config', 'Release', '--parallel', '4'], check=True)
    ctest = str(Path(cmake).with_name('ctest.exe'))
    subprocess.run([ctest, '--test-dir', str(build), '-C', 'Release', '--output-on-failure'], check=True)
    _, _, final_headers = verify_sdk(sdk, pins)
    if final_headers != headers or digest(manifest_path) != sdk_sha:
        raise ValueError('External SDK changed during compilation')
    for pin in source_files + api_pins + provider_pins:
        pinned_file(pin)
    artifacts = []
    for suffix in ('.dll', '.pdb'):
        path = build/'Release'/('SkyrimWorldObserver'+suffix)
        if not path.is_file():
            raise ValueError('Expected matching output missing: '+str(path))
        artifacts.append({'path': str(path), 'sha256': digest(path), 'bytes': path.stat().st_size})
    receipt = {'schemaVersion': 1, 'edition': 'se', 'runtime': pins['runtime'],
               'nativeQualified': False, 'installed': False, 'gameStarted': False,
               'sdkManifest': {'path': str(manifest_path), 'sha256': sdk_sha},
               'sourceFiles': source_files, 'sdkHeaders': headers, 'sdkLibrary': sdk['library'],
               'linkLibraries': sdk['linkLibraries'], 'devbenchApi': api_pins, 'providerAPIs': provider_pins,
               'artifacts': artifacts, 'configureCommand': command}
    (build/'manifest.json').write_bytes((json.dumps(receipt, indent=2)+'\n').encode())
    print(json.dumps({'manifest': str(build/'manifest.json'), 'artifacts': artifacts,
                      'nativeQualified': False}, indent=2))


if __name__ == '__main__':
    main()
