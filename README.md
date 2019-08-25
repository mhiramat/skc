# SKC
Supplementary Kernel Cmdline Userland tool

Masami Hiramatsu <mhiramat@kernel.org>

SKC aims to be an extention of kernel cmdline. This allows user to pass more complex structured commands to kernel boot time. Kernel cmdline is a limited short command line options for kernel boot. It is not enough to configure complex kernel features, like ftrace. User can write a bigger structured ascii text based commands to kernel with SKC.

## SKC Syntax
SKC is a kind of key and value store. Each key consists of period-separated words, e.g. "root.foo.bar.option". And keys which shares common prefix words can be consolideted with brace "{ }" e.g. "root { foo.bar.option; hoge.option; }". Keys can have a value but that is optional. The value is a string data or array of string data.

root.foo = "string data";
root.array = "string1", "string2";
