// Promises to write the whole output, but writes half of it.
[Output("Out"), AlwaysWritten]
Array<float32, 4> output;

[NodeProcess]
void Process() {
    output[0] = 1.0;
    output[1] = 2.0;
}
