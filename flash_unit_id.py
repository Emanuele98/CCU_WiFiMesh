#!/usr/bin/env python3
"""Flash Unit ID to ESP32 NVS partition"""
import subprocess
import sys
import os
import tempfile
import glob

def find_idf_python():
    """Find ESP-IDF Python environment"""
    user_home = os.path.expanduser('~')
    
    # Common ESP-IDF Python locations
    python_locations = [
        # ESP-IDF v5.x standard location
        os.path.join(user_home, '.espressif', 'python_env', 'idf5.*_py*_env', 'Scripts', 'python.exe'),
        # Older versions
        os.path.join(user_home, '.espressif', 'python_env', 'idf*_py*_env', 'Scripts', 'python.exe'),
    ]
    
    for pattern in python_locations:
        matches = glob.glob(pattern)
        if matches:
            # Return the most recent version
            return sorted(matches)[-1]
    
    return sys.executable  # Fallback to current Python

def find_idf_path():
    """Find ESP-IDF installation"""
    idf_path = os.environ.get('IDF_PATH')
    if idf_path and os.path.exists(idf_path):
        return idf_path
    
    user_home = os.path.expanduser('~')
    idf_locations = [
        os.path.join(user_home, 'esp', 'v5.5.1', 'esp-idf'),
        os.path.join(user_home, 'esp', 'esp-idf'),
        'C:\\Users\\degan\\esp\\v5.5.1\\esp-idf',
    ]
    
    for path in idf_locations:
        if os.path.exists(path):
            nvs_gen = os.path.join(path, 'components', 'nvs_flash', 
                                  'nvs_partition_generator', 'nvs_partition_gen.py')
            if os.path.exists(nvs_gen):
                return path
    
    return None

def flash_unit_id(unit_id, port='COM14', chip='esp32'):
    """Flash unit ID to NVS partition"""
    
    nvs_offset = '0x9000'
    nvs_size = '0x6000'
    
    print(f"\n{'='*60}")
    print(f"  Flashing Unit ID: {unit_id} to {port}")
    print(f"{'='*60}\n")
    
    idf_path = find_idf_path()
    if not idf_path:
        print("❌ Error: ESP-IDF not found!")
        sys.exit(1)
    
    idf_python = find_idf_python()
    print(f"📁 Using ESP-IDF: {idf_path}")
    print(f"🐍 Using Python: {idf_python}")
    
    nvs_gen = os.path.join(idf_path, 'components', 'nvs_flash', 
                          'nvs_partition_generator', 'nvs_partition_gen.py')
    
    with tempfile.TemporaryDirectory() as tmpdir:
        csv_file = os.path.join(tmpdir, 'nvs_data.csv')
        bin_file = os.path.join(tmpdir, 'nvs.bin')
        
        # Create CSV
        csv_content = f"""key,type,encoding,value
storage,namespace,,
unit_id,data,u32,{unit_id}
"""
        with open(csv_file, 'w') as f:
            f.write(csv_content)
        
        print("🔧 Generating NVS binary...")
        
        # Generate binary using ESP-IDF Python
        result = subprocess.run(
            [idf_python, nvs_gen, 'generate', csv_file, bin_file, nvs_size],
            capture_output=True, text=True
        )
        
        if result.returncode != 0:
            print(f"❌ Generation failed:\n{result.stderr}")
            print(f"\nStdout:\n{result.stdout}")
            sys.exit(1)
        
        print(f"⚡ Flashing to {port}...")
        
        # Flash to device using ESP-IDF Python
        result = subprocess.run([
            idf_python, '-m', 'esptool',
            '--chip', chip,
            '--port', port,
            '--baud', '460800',
            'write_flash', nvs_offset, bin_file
        ], capture_output=True, text=True)
        
        if result.returncode != 0:
            print(f"❌ Flash failed:\n{result.stderr}")
            print(f"\nStdout:\n{result.stdout}")
            sys.exit(1)
        
        print(f"\n✅ SUCCESS! Unit ID {unit_id} flashed to {port}")
        print("🔄 Device restarting...\n")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python flash_unit_id.py <UNIT_ID> [PORT] [CHIP]")
        print("Example: python flash_unit_id.py 1001 COM14 esp32")
        sys.exit(1)
    
    unit_id = int(sys.argv[1])
    port = sys.argv[2] if len(sys.argv) > 2 else 'COM14'
    chip = sys.argv[3] if len(sys.argv) > 3 else 'esp32'
    
    flash_unit_id(unit_id, port, chip)