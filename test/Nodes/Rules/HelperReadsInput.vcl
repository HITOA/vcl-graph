// Rule 1: a helper names an input.
[Input("In")]
float32 input = 0.0;
[Output("Out")]
float32 output = 0.0;

float32 Twice() {
    return input * 2.0;
}

[NodeProcess]
void Process() {
    output = Twice();
}
