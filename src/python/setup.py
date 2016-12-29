from distutils.core import setup, Extension

mocarpc = Extension('mocarpc', 
                    sources = ['MocaRPC.cc'],
                    define_macros = [('__STDC_LIMIT_MACROS', '')],
                    include_dirs = ['../cpp/include'],
                    library_dirs = ['../../output/linux.amd64.release/lib', '/usr/local/libuv/lib'],
                    libraries = ['MocaRPC', 'uv', 'uuid', 'z'],
                    extra_compile_args = ['-Werror', '-Wall'])

setup (name = 'python-mocarpc', version = '0.1', 
       description = 'MocaRPC Python Binding',
       ext_modules = [mocarpc])
