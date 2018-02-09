from distutils.core import setup, Extension

mocharpc = Extension('mocharpc', 
                    sources = ['MochaRPC.cc'],
                    define_macros = [('__STDC_LIMIT_MACROS', '')],
                    include_dirs = ['../cpp/include'],
                    library_dirs = ['../../output/linux.amd64.release/lib', '/usr/local/libuv/lib'],
                    libraries = ['MochaRPC', 'uv', 'uuid', 'z'],
                    extra_compile_args = ['-Werror', '-Wall'])

setup (name = 'python-mocharpc', version = '0.1', 
       description = 'MochaRPC Python Binding',
       ext_modules = [mocharpc])
