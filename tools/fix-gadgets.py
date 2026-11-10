#!/usr/bin/env python3
"""apps/seven-gadgets: the gadget pictures (data/, seven-weather/images) are Windows 7's and come from the user's
ISO (extract/recipes.json), so the source tree has none; install those folders only when present. Idempotent."""
import os
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'apps', 'seven-gadgets', 'CMakeLists.txt')
s = open(p).read()
for line in ('install(DIRECTORY data/ DESTINATION share/seven-gadgets)',
             'install(DIRECTORY seven-weather/images DESTINATION share/seven-gadgets/weather)'):
    d = line.split()[0].split('(DIRECTORY')[0]
    folder = line.split('DIRECTORY ')[1].split(' ')[0].rstrip('/')
    guarded = 'if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/%s)\n    %s\nendif()' % (folder, line)
    if line in s and guarded not in s:
        s = s.replace(line, guarded)
open(p, 'w').write(s)
print('gadgets: picture folders optional')
