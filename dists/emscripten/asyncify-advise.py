
from sqlite3 import connect
import sys
import json
import re
import sys
import json
from cxxfilt import demangle

def string_unescape(s, encoding='utf-8'): ## the output is escaped
    replacements=[["20"," "],["28","("],["29",")"],["2c",","],["5b","["],["5d","]"]]
    for replacement in replacements:
        s = s.replace("\\"+replacement[0],replacement[1])
   
    if "\\" in s:
        sys.exit("Couldn't unescape "+s)
    return s

def remove_prefix(text, prefix):
    while text.startswith(prefix):
        text =  text[len(prefix):]
    return text  # or whatever


class color:
   PURPLE = '\033[95m'
   CYAN = '\033[96m'
   DARKCYAN = '\033[36m'
   BLUE = '\033[94m'
   GREEN = '\033[92m'
   YELLOW = '\033[93m'
   RED = '\033[91m'
   BOLD = '\033[1m'
   UNDERLINE = '\033[4m'
   END = '\033[0m'

main_module_exports = open("./dists/emscripten/asyncify-advise.txt")

lines = main_module_exports.readlines()
callees = {}
all_callers = []
i =0
instrumented_functions = []
for line in lines:
#print '"Hello,\\nworld!"'
    if(line.startswith("[asyncify] ")):
        for sub_line in line.split("[asyncify] "):
            sub_line = sub_line.strip()
            if(len(sub_line) > 0):
                result = re.search(r"^(\S+)('is an import that can change the state')?('can change the state due to ')?(\S+)?\n", sub_line)
                result = re.search(r"^(\S+)?(?:[\s]{0,2}is an import that can change the state)?(?:[\s]{0,2}can change the state due to )?(?:initial scan)?(\S*)$", sub_line)
                if(result):
                    for group in result.groups():
                        if (group and len(group)> 0):
                            instrumented_functions.append(string_unescape(group).rstrip(".1"))
                else:
                    print(line)
                    sys.exit("Couldn't parse: "+sub_line)

# deduplicate
instrumented_functions = list(dict.fromkeys(instrumented_functions))
instrumented_functions.sort()


with open('./dists/emscripten/asyncify-advise.json', 'w') as f:
    # indent=2 is not needed but makes the file human-readable 
    # if the data is nested
    json.dump(instrumented_functions, f, indent=2) 





main_module_exports = open("./dists/emscripten/main-module-exports.txt")
testbed_imports = open("./dists/emscripten/testbed-imports.txt")

lines = main_module_exports.readlines()
callees = {}
all_callers = []
i =0
symbols = {}
def add_symbol(mangled):
    demangled = demangle(mangled.removeprefix("env.").removeprefix("GOT.func."))
    if(demangled in symbols):
        symbols[demangled].append("env."+mangled)
        # deduplicate
        symbols[demangled] = list(dict.fromkeys(symbols[demangled]))
    else:
        symbols[demangled] = ["env."+mangled]
    #print(demangled + " " + str(symbols[demangled]))

for line in lines:
   
    # regex for Globals (result in groups[5])
    #result = re.search(r"^ - (\w+)\[(\d+)\] (i32 mutable=[0-1] )?(sig=\d+ )?(\<([\$\w]+)\> )?- init i32=\d+\n", line)

    
    '''
    # regex for Functions 
    result = re.search(r"^ - (?:\w+)\[\d+\] sig=\d+(?: \<(.+)\>)?$", line)
    if(result):
        for group in result.groups():
            if (group and len(group)> 0):
                #add_symbol(group)
                pass
    '''
    # Regex for Exports
    result = re.search(r"^ - (?:\w+)\[\d+\](?: \<(?:[\$\w]+)\>)? -\> \"([\$\w]+)\"$", line)
    if(result):
        for group in result.groups():
            if (group and len(group)> 0):
                add_symbol(group)
        
    elif line.strip() != "":                    
        print("Ignoring Line: "+line.strip())

    #Regex for Imports 
    '''
    result = re.search(r"^ - (?:\w+)\[\d+\] (?:i32 mutable=[0-1])?(?:sig=\d+)?(?: \<(\S+)\>)? \<- (\S+)$", line)
    if(result):
        for group in result.groups():
            if (group and len(group)> 0):
                add_symbol(group)
            
    elif line.strip() != "":                    
        print("Ignoring Line: "+line.strip())
    '''


asyncify_imports= []
#print(instrumented_functions)
print(symbols)
for obj in instrumented_functions:
    if( obj in symbols):
       # print("found: "+obj)
       if "TinyGL" not in obj and not obj.startswith("std::") and not obj.startswith("SDL_"): ## we strip some we know won't be part of the imports (as the argument list will get too long otherwise)
            asyncify_imports.extend(symbols[obj])
    else:
       
        #sys.exit()
        pass;

#print(asyncify_imports)
# TODO: To make this work also with debug builds which don't mangle, we shoudl also add the cleartext names here (maybe they need to be escaped? have to check with binaryen and test)
with open('./dists/emscripten/asyncify-imports.txt', 'w') as f:
        asyncify_import_str = "'[\"" + "\",\"".join(asyncify_imports) + "\"]'"
        f.write(f"{asyncify_import_str}")