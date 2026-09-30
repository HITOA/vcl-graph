// Rule 2: [NodeReset] reads an input.
[Input("In")]
float32 input = 0.0;
[Output("Out")]
float32 output = 0.0;

[NodeReset]
void Reset() {
    output = input;
}

[NodeProcess]
void Process() {
    output = input;
}
