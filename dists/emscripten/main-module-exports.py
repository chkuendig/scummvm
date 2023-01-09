
a_file = open("./dists/emscripten/main-module-exports.txt")

import re
import sys
import json
from cxxfilt import demangle
lines = a_file.readlines()
callees = {}
all_callers = []
i =0
symbols = {}
def add_symbol(mangled):
    demangled = demangle(mangled)
    if(demangled in symbols):
        symbols[demangled].append(mangled)
        # deduplicate
        symbols[demangled] = list(dict.fromkeys(symbols[demangled]))
    else:
        symbols[demangled] = [mangled]

for line in lines:
    # Regex for Exports (symbols in groups[4]  + groups[3])
    result = re.search(r"^ - (\w+)\[(\d+)\]( \<([\$\w]+)\>)? -\> \"([\$\w]+)\"\n", line)
    # regex for Globals (result in groups[5])
    result = re.search(r"^ - (\w+)\[(\d+)\] (i32 mutable=[0-1] )?(sig=\d+ )?(\<([\$\w]+)\> )?- init i32=\d+\n", line)

    # regex for Functions (result in groups[2])
    result = re.search(r"^ - (\w+)\[(\d+)\] sig=\d+ \<([\$\w]+)\>\n", line)
    if(result):
        groups =  result.groups()
        if(groups[2]):
            add_symbol(groups[2])
   
    elif line.strip() != "":
        print("Ignoring Line: "+line.strip())

print(symbols)
asyncify_imports= []

with open("./dists/emscripten/asyncify-imports.json", 'r') as f:
    asyncify_objs = json.load(f)
   # print(asyncify_objs)
    for obj in asyncify_objs:
        obj = obj.rstrip(".1")
        if( obj in symbols):
            print("found: "+obj)
            asyncify_imports.extend(symbols[obj])
        #else:
        #    print("not found:  (not an export?) "+obj)

print(asyncify_imports)
with open('./dists/emscripten/asyncify-imports.txt', 'w') as f:
        asyncify_import_str = "'[\"" + "\",\"".join(asyncify_imports) + "\"]'"
        f.write(f"{asyncify_import_str}")