[Input("A")]
float32 inputA = 0.0;
[Input("B")]
float32 inputB = 0.0;

[Output("Output")]
float32 output = 0.0;

[NodeProcess]
void Process() {
    output = inputA + inputB;
}