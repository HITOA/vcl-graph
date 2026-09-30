// Allowed: the entry point passes an output to a helper by reference, and the helper uses state.
[Input("In")]
float32 input = 0.0;
[Output("Out")]
float32 output = 0.0;

float32 total = 0.0;

void Accumulate(float32 value, inout float32 result) {
    total += value;
    result = total;
}

[NodeProcess]
void Process() {
    Accumulate(input, output);
}
