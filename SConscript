Import('env')

# Avoid doing dependency checks on garden files.
cpp=[]
flg=[]
for p in env['CPPPATH']:
    if p.startswith('/proj'):
        flg.append('-I%s' % p)
    else:
        cpp.append(p)
env.Replace(CPPPATH=cpp)
env.Append(CFLAGS=flg, CXXFLAGS=flg)

env.Append(
        CFLAGS='-O2 -g',
        CXXFLAGS='-std=c++03 -O2 -Wall -g',
        )
# monobuild is pedantic.  If not monobuild then -Werror is a good thing
if 'MONOBUILD' not in env or not env['MONOBUILD']:
   env.Append(CXXFLAGS='-Werror')

env.SConsignFile('%s/.sconsign' % (env['OBJDIR'].strip('#')))

env.SConscript('src/SConscript')
env.SConscript('tests/SConscript')

opts=Variables()
opts.Add("MSYS_WITHOUT_PYTHON", "without python?")
opts.Update(env)

if env.get("MSYS_WITHOUT_PYTHON"):
    print "MSYS_WITHOUT_PYTHON set; will not build python extensions or tools."
else:
    env.SConscript('python/SConscript')
    env.SConscript('tools/SConscript')

