// Invalid: passes its input to a helper that may modify it.
[Input("In")]
float32 input = 0.0;

[Output("Out")]
float32 output = 0.0;

void Double(inout float32 value) {
    value = value * 2.0;
}

[NodeProcess]
void Process() {
    Double(input);
    output = input;
}
