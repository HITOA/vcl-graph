// Always writes its output, which has an initializer of its own.
[Input("In")]
float32 input = 0.0;

[Output("Out")]
float32 output = 7.0;

[NodeProcess]
void Process() {
    output = input * 2.0;
}
