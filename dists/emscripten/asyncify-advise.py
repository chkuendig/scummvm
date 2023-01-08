
from sqlite3 import connect
import sys
import json

def string_escape(s, encoding='utf-8'): ## the output is escaped
    replacements=[["20"," "],["28","("],["29",")"],["2c",","],["5b","["],["5d","]"]]
    for replacement in replacements:
        s = s.replace("\\"+replacement[0],replacement[1])
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

a_file = open("./dists/emscripten/asyncify-advise.txt")

lines = a_file.readlines()
callees = {}
all_callers = []
i =0
for line in lines:
#print '"Hello,\\nworld!"'
    if(line.startswith("[asyncify] ")):
            clean_str = string_escape(line.replace("[asyncify]","").strip())
            if(clean_str.find("can change the state due to ")) == -1:
                print("IGNORE:" +line)
            else:
                print(clean_str)
                connection = clean_str.split("can change the state due to")
                caller = connection[0].strip()
                callee = connection[1].strip()
               # caller = remove_prefix(caller,"void ")
               # callee = remove_prefix(callee,"void ")
                if(callee in callees):
                    callees[callee].append(caller)
                else:
                    callees[callee] = [caller]
                all_callers.append(caller)
                #print(caller,"-->",callee)
    else:
        sys.exit("File corrupt")
        
callee_namespaces = {}
caller_counts = {}
for callee, callers in callees.items() :
    caller_counts[callee] = len(callers)
    if(callee == "initial scan" or callee == "operator new(unsigned long)"):
        print(color.BOLD + callee + color.END)
        print(len(callers))
    continue
    if(len(callee.split("::")) >2):
        print(color.BOLD + callee + color.END)
        print(callers)
    print(color.BOLD + callee + color.END)
    print(callers)
    if(callee.find("::") > -1):
        callee_namespace = callee.split("::")[0] # FIXME: This fails for names which have a class only in the parameters
        if(callee_namespace in callee_namespaces):
            callee_namespaces[callee_namespace].append(callee)
        else:
            callee_namespaces[callee_namespace] = [callee]
            i+=1

print("+++++++++++++++++++++++++++++++++++++")
print("+++++++++++++++++++++++++++++++++++++")
print("+++++++++++++++++++++++++++++++++++++")
callee_namespaces = dict(sorted(callee_namespaces.items()))
for callee_namespace, namespace_callees in callee_namespaces.items() :
    continue
    
    print(color.BOLD + callee_namespace + color.END)
    print(callees)

#engine_namespaces = ("Touche::","HYPNO_MIS_parse","AGS::","Testbed::","Hadesch::","Composer::","MutationOfJB::","Cine::","CGE::","Lab::","Hugo::","Hypno::","Draci::","Lilliput::","CGE2::","Toon::","MacVenture::","Petka::","Groovie::","Wage::","Trecision::","Saga::","Access::","Parallaction::","HDB::","Tucker::","Dragons::","Prince::","Adl::","Tinsel::","Tony::","Sludge::","Pink::","TwinE::","Myst3::","Sword2::","Hopkins::","Pegasus::","Mohawk::","StarTrek::","Xeen::","Sherlock::","Kyra::","Asylum::","Grim::","LastExpress::","BladeRunner::","MADS::","Ultima::","Neverhood::","Buried::","Director::","Chewy::","AGS3::","Glk::","Gob::","TsAGE::","NGI::","CryOmni3D::","Gnap::","DreamWeb::","Sci::","Voyeur::","Made::","Wintermute::","ZVision::","TeenAgent::","DM::","Scumm::","Mortevielle::","Toltecs::","Cruise::","Saga2::","Lure::","Kingdom::","Sky::","AGOS::","Queen::","Sword1::","Avalanche::","Drascula::","Supernova::","Cryo::","Agi::","Illusions::","Bbvs::","ICB::","Private::")
#main_module_namespaces = ("Common::","EuphonyPlayer","Modules","Image","DefaultAudioCDManager","TownsPC98","MainMenuDialog","VGMInstr","SdlTimerManager","SdlWindow","OPL::","MidiParser","SegaAudioInterface","OpenGL","void Common","PC98AudioCore","OSystem","CMSEmulator","Math","TownsAudioInterface","Video","SDL","TinyGL","MusicDevice","Audio","MidiDriver","MT32Emu","Common","lua","Graphics","Engine","GUI")
print("raw caller counts per callee:")
#print(dict(sorted(caller_counts.items(), key=lambda item: item[1])))
print("+++++++++++++++++++++++++++++++++++++")
print("caller counts per callee (excl. engine callees):")
#filtered_dict = {k:v for (k,v) in caller_counts.items() if not k.startswith(engine_namespaces) or k.startswith(main_module_namespaces) }
#TODO: We should filter only the ones which cross from main_module to engine namespaces 

#filtered_dict = {k:v for (k,v) in caller_counts.items() if not any(namespace in  k for namespace in engine_namespaces) and  not any(namespace in  k for namespace in main_module_namespaces)}
print(dict(sorted(caller_counts.items(), key=lambda item: item[1])))

print(len(caller_counts.items()))

print("+++++++++++++++++++++++++++++++++++++")
print("+++++++++++++++++++++++++++++++++++++")
print(len(caller_counts.items()))
print(len(callee_namespaces))
print(i)

print(len(all_callers))
#with open('./dists/emscripten/asyncify-imports.txt', 'w') as f:
#        asyncify_import_str = "'[\"" + "\",\"".join(all_callers) + "\"]'"
#        f.write(f"{asyncify_import_str}")


with open('./dists/emscripten/asyncify-imports.json', 'w') as f:
    # indent=2 is not needed but makes the file human-readable 
    # if the data is nested
    json.dump(all_callers, f, indent=2) 