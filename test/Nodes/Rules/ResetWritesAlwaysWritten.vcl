// Rule 2 (warning): what [NodeReset] writes to an [AlwaysWritten] output is never observed.
[Output("Out"), AlwaysWritten]
float32 output;

[NodeReset]
void Reset() {
    output = 1.0;
}

[NodeProcess]
void Process() {
    output = 2.0;
}
