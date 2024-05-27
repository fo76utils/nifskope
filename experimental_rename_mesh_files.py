import os
import shutil
import datetime
# This script is 100% experimental and untested
# It might rename meshes that were modified directly
# Totally untested


def load_mapping(file_path):
    mapping = {}
    with open(file_path, 'r') as file:
        for line in file:
            key, value = line.strip().split(':')
            mapping[key] = value if value else None
    return mapping

def process_files(folder, mapping):
    timestamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    log_file = f'rename_log_{timestamp}.txt'

    with open(log_file, 'w') as log:
        for subdir in next(os.walk(folder))[1]:
            subdir_path = os.path.join(folder, subdir)
            for filename in os.listdir(subdir_path):
                if filename.endswith('.mesh'):
                    file_base = filename[:-5]  # Remove the .mesh extension
                    key = f"{subdir}\\{file_base}"
                    if key in mapping:
                        new_base = mapping[key]
                        if new_base:
                            old_path = os.path.join(subdir_path, filename)
                            new_subdir, new_file_base = os.path.split(new_base)
                            new_dir_path = os.path.join(folder, new_subdir)
                            os.makedirs(new_dir_path, exist_ok=True)
                            new_path = os.path.join(new_dir_path, f'{new_file_base}.mesh')
                            shutil.move(old_path, new_path)
                            log.write(f'Renamed: {key}.mesh to {new_base}.mesh\n')
                        else:
                            log.write(f'No value for: {key}.mesh\n')
                    else:
                        log.write(f'No match for: {key}.mesh\n')

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description='Rename .mesh files based on a mapping table.')
    parser.add_argument('folder', type=str, help='The folder to scan for .mesh files')
    parser.add_argument('mapping_file', type=str, help='The file containing the key-value pairs')

    args = parser.parse_args()

    mapping = load_mapping(args.mapping_file)
    process_files(args.folder, mapping)
