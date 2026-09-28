[Parameter("Factor")]
const float32 factor = 2.0;

[Input("In")]
float32 input = 0.0;

[Output("Out")]
float32 output = 0.0;

[NodeProcess]
void Process() {
    output = input * factor;
}
