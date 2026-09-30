// LoopAlwaysWritten without the promise: the output is always written, but nobody says so.
[Input("In")]
float32 input = 0.0;
[Output("Out")]
Array<float32, 1024> output;

Array<float32, 1024> history;
uint32 index = 0;

[NodeProcess]
void Process() {
    for (uint32 i = 0; i < 1024; ++i) {
        output[i] = history[(index + i) % 1024] * input;
        history[(index + i) % 1024] = output[i] + 0.25;
    }
    index = (index + 1) % 1024;
}
