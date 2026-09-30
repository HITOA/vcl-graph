// A broken promise: the output is read before being written.
[Input("In")]
float32 input = 0.0;
[Output("Out"), AlwaysWritten]
float32 output;

[NodeProcess]
void Process() {
    output = output * 0.5 + input;
}
