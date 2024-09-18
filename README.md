# Node.js Trace (v20.17.0)

This repository contains a fork of Node.js v20 (commit: efbec04208a3c8588d4e7f076c47d29dddf47976) aimed at collecting the call graph of a Node.js execution.

## Overview

Node.js Trace enhances the standard Node.js runtime to generate detailed call graphs during execution. 
Each node process will create a `cg.tsv` and `funcs.tsv`. Those files can be combained into one callgraph using the script in `nodeCG2endor`.
This fork produces two primary output files:

1. `cg.tsv`: Contains the call graph data
2. `funcs.tsv`: Contains function information (name, position, and path)

## Features

- Two call graph collection modes:
  1. Stack trace-based: More precise but slower
  2. Function entry/exit-based: Faster but less precise
- Configurable tracing depth
- Option to include or exclude Node.js internal functions

## Configuration

Configure Node.js Trace using environment variables:

| Variable | Description | Default |
|----------|-------------|---------|
| `TRACE_ALL` | Set to `1` to trace all functions, including Node.js internals | `0` |
| `TRACE_DEPTH` | Maximum depth of the collected stack (deeper = slower) | `100` |

**Note**: If `TRACE_DEPTH` is set to a value less than 2, the call graph will be based on function entry and exit points.

## Usage

1. Clone this repository:
   ```
   git clone https://github.com/your-username/nodejs-trace.git
   cd nodejs-trace
   ```

2. Build Node.js Trace:
   ```
   ./configure
   make -j4
   ```

3. Run your Node.js application with tracing:
   ```
   TRACE_ALL=1 TRACE_DEPTH=10 ./node your-app.js
   ```

However, the recommend usage is to put this version of node inside your path since some testing framework or typescript are lunching multiple processes of node.
Adding node in the path garantee that the full execution will be executed.

4. Analyze the generated `cg.tsv` and `funcs.tsv` files.


## Format

### `func.tsv`
```tsv
func id    name    func file id    start    end    path
```
```tsv
0               0       0       195     /private/tmp/js_vuln_test/simple.js
1               1       0       195     /private/tmp/js_vuln_test/simple.js
2       one     2       12      60      /private/tmp/js_vuln_test/simple.js
3       two     3       74      109     /private/tmp/js_vuln_test/simple.js
4       three   4       125     166     /private/tmp/js_vuln_test/simple.js
```


### `cg.tsv`

Stack mode
```tsv
stack IDs   func id
```

```tsv
0,1,2,0	0
4,0	1
5,4	2
6,5	3
6	3
4	1
5	2
6	3
6	3
5	3
4	3
```

IN OUT mode

```tsv
I0
I1
I2
I3
O3
I3
O3
O2
O1
I1
I2
I3
O3
I3
O3
O2
I3
O3
O1
I3
O3
O0
```

## Convert Node CG to Endor CG

The folder `nodeCG2endor` contains the script to convert the collected callgraph to Endor callgraph format.
The script will combine all the `cg.tsv` and `func.tsv` present in the project folder. 

```bash
python3 node nodeCG2endor/nodeCG2endor.py <path_to_project>
```

### Output

```json
{
  "packageName": "<name>@<version>",
  "createTime": "",
  "functionMap": {
    "0": "javascript://js_vuln_test$1.0.0/[js_vuln_test:1.0.0:js_vuln_test/simple]/()",
    "1": "javascript://js_vuln_test$1.0.0/[js_vuln_test:1.0.0:js_vuln_test/simple]/()",
    "2": "javascript://js_vuln_test$1.0.0/[js_vuln_test:1.0.0:js_vuln_test/simple]/one()",
    "3": "javascript://js_vuln_test$1.0.0/[js_vuln_test:1.0.0:js_vuln_test/simple]/two()",
    "4": "javascript://js_vuln_test$1.0.0/[js_vuln_test:1.0.0:js_vuln_test/simple]/three()"
  },
  "callgraph": {
    "1": {
      "nodes": [
        2,
        4
      ]
    },
    "2": {
      "nodes": [
        3,
        4
      ]
    },
    "3": {
      "nodes": [
        4
      ]
    }
  }
}
```
## Performance Considerations

- Enabling tracing will impact performance. The impact varies based on the chosen mode and trace depth.
- For production environments, consider using this fork selectively or only on non-critical paths.

## License

Node.js is available under the
[MIT license](https://opensource.org/licenses/MIT). Node.js also includes
external libraries that are available under a variety of licenses.  See
[LICENSE](https://github.com/nodejs/node/blob/HEAD/LICENSE) for the full
license text.

[Code of Conduct]: https://github.com/nodejs/admin/blob/HEAD/CODE_OF_CONDUCT.md
[Contributing to the project]: CONTRIBUTING.md
[Node.js website]: https://nodejs.org/
[OpenJS Foundation]: https://openjsf.org/
[Strategic initiatives]: doc/contributing/strategic-initiatives.md
[Technical values and prioritization]: doc/contributing/technical-values.md
[Working Groups]: https://github.com/nodejs/TSC/blob/HEAD/WORKING_GROUPS.md
