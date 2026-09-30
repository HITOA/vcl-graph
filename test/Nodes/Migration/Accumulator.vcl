// Counts in steps. "Size" gives the state its shape, "Step" doesn't.
[Parameter("Size")]
const uint32 size = 1;
[Parameter("Step")]
const float32 step = 1.0;

[Output("Out")]
float32 output = 0.0;

float32 count = 0.0;
Array<float32, size> history;

[NodeProcess]
void Process() {
    count += step;
    history[0] = count;
    output = count;
}
