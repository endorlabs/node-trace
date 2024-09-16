# Node.js Trace (v20.17.0)

This repository contains a fork of Node.js v20 (commit: efbec04208a3c8588d4e7f076c47d29dddf47976) aimed at collecting the call graph of a Node.js execution.

## Overview

Node.js Trace enhances the standard Node.js runtime to generate detailed call graphs during execution. This fork produces two primary output files:

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
| `TRACE_DEPTH` | Maximum depth of the collected stack (deeper = slower) | `Infinity` |

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

4. Analyze the generated `cg.tsv` and `funcs.tsv` files.

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
