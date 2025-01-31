import sys
import os
import json

def func2name(data, known_funcs):
  prefix = ''
  for k, v in known_funcs.items():
    if (v['id'] == data['id'] and v['position']['file'] == data['position']['file']) or v['position']['file'] != data['position']['file'] or v['position']['start'] == 0:
      continue
    if v['position']['file'] == data['position']['file'] and v['position']['start'] <= data['position']['start'] and v['position']['end'] >= data['position']['end']:
      prefix = f"{v['func_prefix']}{v['func_name']}/"
  name = data['name']
  if name == "":
    if data['position']['start'] == 0:
      name = ''
    else:
      name = f"anonymous_function_{data['position']['line']}_{data['position']['column']}"
  # else:
  #   name += f"_{data['position']['line']}_{data['position']['column']}"
  name += "()"
  name = name.replace("Object.<anonymous>.","").replace(".","/")
  data['func_name'] = name
  data['func_prefix'] = prefix
  name = prefix + name
  if data['position']['file'].startswith('node:'):
    return f"node://node/[node:{data['position']['file'].replace('node:', 
    '')}]/{name}"
  pack, version, pack_f_path = get_package_npm_version(data['position']['file'])
  return f"javascript://{pack}${version}/[{pack}:{version}:{pack_f_path.replace(".js", "")}]/{name}"

package_cache = {}
def get_package_npm_version(p):
  p_split = p.split('/')
  p_split_len = len(p_split)
  for i in range(len(p_split)):
    pp = '/'.join(p_split[:p_split_len-1-i])
    package = '/'.join(p.split("/")[p_split_len-2-i:]).replace(".js", "").replace(".ts", "")
    if pp+package in package_cache:
      return package_cache[pp+package]
    if os.path.exists(f"{pp}/package.json"):
        with open(f"{pp}/package.json") as f:
            data = json.load(f)
            if 'name' not in data or 'version' not in data:
              continue
            name = data['name']
            if ('/' in name):
              name = name.replace('/', '::')
            package_cache[pp+package] = (name, data['version'], package)
            return package_cache[pp+package]
    p_split = p_split[:p_split_len-1-i]
  return ('<name>', '<version>', p)


def readFuncInfoFile(p='func.tsv'):
    out = {}
    with open(p, 'r') as f:
      for line in f:
        line = line.rstrip()
        (func_id, func_name, line_nb, column, func_file_id, start, end, file, time) =  line.split('\t')
        file = file.strip().replace('file://', '')
        func_id = int(func_id)
        func_key = f"{func_file_id}_{start}_{end}_{file}"
        out[func_key] = {
            'id': int(func_id),
            'name': func_name,
            'internal': 'node_modules' not in file and not file.startswith('node:'),
            'position': {
              'line': int(line_nb),
              'column': int(column),
              'start': int(start),
              'end': int(end),
              'file': file
            }
          }
        out[func_key]['uri'] = func2name(out[func_key], out)
    return out

def readCGFile(global_id_map, p='cg.tsv', function_calls={}):
  print(f"Process file: {p}")
  call_in_stack = []
  with open(p, 'r') as f:
    for line in f:
      line = line.rstrip()
      if len(line) == 0:
         continue
      if line[0] in ['I', 'O']:
        called_func = global_id_map[int(line[1:])]
        if line[0] == 'I':
          if len(call_in_stack) > 0:
            if call_in_stack[-1] not in function_calls:
              function_calls[call_in_stack[-1]] = set()
            function_calls[call_in_stack[-1]].add(called_func)
          call_in_stack.append(called_func)
        elif line[0] == 'O':
          index = len(call_in_stack) - 1
          while call_in_stack[index] != called_func:
            call_in_stack.pop()
            index -= 1
          call_in_stack.pop()
      else:
        split_line = line.split('\t')
        if len(split_line) != 3:
          continue
        (caller_funct, called_func, time) = split_line
        called_func = global_id_map[int(called_func)]
        caller_funct = global_id_map[int(caller_funct)]
        if caller_funct not in function_calls:
          function_calls[caller_funct] = set()
        function_calls[caller_funct].add(called_func)
  return function_calls      

def merge_func_infos(func_infos):
  out = {}
  map_id = {}
  global_id = 0
  global_id_map = {}
  for proc in func_infos:
    map_id[proc] = {}
    for k, v in func_infos[proc].items():
      new_id = global_id
      if k not in global_id_map:
        global_id += 1
        global_id_map[k] = new_id
      else:
        new_id = global_id_map[k]
      out[new_id] = v
      map_id[proc][v['id']] = new_id
  return out, map_id
if __name__ == '__main__':
  project_root = sys.argv[1]
  func_infos = {}
  for file in os.listdir(project_root):
    if file.endswith('.tsv') and file.startswith('func_'):
      proc = file.split('_')[1].replace('.tsv', '')
      if not os.path.exists(os.path.join(project_root, f"cg_{proc}.tsv")):
        continue
      func_infos[proc] = (readFuncInfoFile(os.path.join(project_root, file)))
  func_info, map_id = merge_func_infos(func_infos)
  
  cg = {}
  for proc in func_infos:
    readCGFile(map_id[proc], os.path.join(project_root, f"cg_{proc}.tsv"), cg)
  name, version = ('<name>', '<version>')
  if os.path.exists(f"{project_root}/package.json"):
    name, version, _ = get_package_npm_version(project_root)

  functionMap = {}
  for id, v in func_info.items():
    functionMap[id] = v['uri']
  callgraph = {}
  for k, v in cg.items():
    callgraph[k] = {
      'nodes': list(v)
    }
  json.dump({
      "packageName": f"{name}@{version}",
      "createTime": "",
      "functionMap": functionMap,
      "callgraph":  callgraph
  }, open(os.path.join(project_root, "cg.json"), 'w'), indent=2)