"""Reuse exact qualified native sensor allocator/PAR/cache checks for CAMSS."""
from pathlib import Path
import hashlib,json
from PowerSource import function
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
def derive():
    spec=json.loads((HERE/'dma-source.json').read_text());texts={}
    for name,digest in spec['qualified_native_memory'].items():
        data=(ROOT/name).read_bytes()
        if hashlib.sha256(data).hexdigest()!=digest:raise ValueError('Qualified original allocator/PAR code changed: '+name)
        texts[Path(name).name]=data.decode().replace('\r\n','\n')
    source=texts['NativeMemoryMethods.inc'];definitions=[];checks=[]
    for signature in ('static uint32_t sensor_output_bits(','static int sensor_memory_verify('):
        body=function(source,signature);derived=body.replace('EldenSensorNativeMemory','EldenCameraNativeDma').replace('sensor_memory_','camera_dma_').replace('sensor_output_bits','camera_output_bits').replace('elden_sensor_memory_','elden_camera_dma_')
        definitions.append(derived);checks.append({'signature':signature,'original_sha256':hashlib.sha256(body.encode()).hexdigest(),
            'derived_sha256':hashlib.sha256(derived.encode()).hexdigest(),'type_and_symbol_prefix_only':True})
    native=texts['NativeMemory.cpp'];arch=[]
    for signature in ('static uint64_t sensor_memory_translate(','static uint32_t sensor_memory_cache_line(','static void sensor_memory_clean(','static void sensor_memory_barrier('):
        body=function(native,signature);derived=body.replace('sensor_memory_','camera_dma_');arch.append(derived)
        checks.append({'signature':signature,'original_sha256':hashlib.sha256(body.encode()).hexdigest(),
            'derived_sha256':hashlib.sha256(derived.encode()).hexdigest(),'symbol_prefix_only':True})
    return {'DmaOriginalChecks.inc':('/* SPDX-License-Identifier: BSD-2-Clause-Patent */\n'+'\n\n'.join(definitions)+'\n').encode(),
        'DmaArchitecture.inc':('/* SPDX-License-Identifier: BSD-2-Clause-Patent */\n'+'\n\n'.join(arch)+'\n').encode()},checks
