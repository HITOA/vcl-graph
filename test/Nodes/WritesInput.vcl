// Invalid: writes its input.
[Input("In")]
float32 input = 0.0;

[Output("Out")]
float32 output = 0.0;

[NodeProcess]
void Process() {
    input = input * 2.0;
    output = input;
}
