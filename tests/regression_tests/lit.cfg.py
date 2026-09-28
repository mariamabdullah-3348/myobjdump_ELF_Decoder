import os
import lit.formats

config.name = "MyObjdump"
config.test_format = lit.formats.ShTest(execute_external=False)
config.suffixes = ['.test']
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root   = os.path.join(config.test_source_root, 'Output')

# Path to the built binary — adjust for platform
myobjdump_bin = os.environ.get('MYOBJDUMP_EXE')
if not myobjdump_bin:
    project_root = os.path.normpath(os.path.join(config.test_source_root, '..', '..'))
    possible_paths = [
        os.path.join(project_root, 'build', 'myobjdump.exe'),
        os.path.join(project_root, 'build-win', 'Release', 'myobjdump.exe'),
        os.path.join(project_root, 'build', 'myobjdump'),
        os.path.join(project_root, 'build-linux', 'myobjdump'),
        os.path.join(project_root, 'build-linux-asan', 'myobjdump'),
    ]
    for p in possible_paths:
        if os.path.exists(p):
            myobjdump_bin = p
            break
    if not myobjdump_bin:
        myobjdump_bin = "myobjdump" # fallback to PATH

config.substitutions.append(('%myobjdump', myobjdump_bin))
config.substitutions.append(('%data', os.path.normpath(
    os.path.join(config.test_source_root, '..', '..', 'data')
)))
