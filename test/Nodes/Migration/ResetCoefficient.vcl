// Like Grog's EQ filters: [NodeReset] computes a value from a parameter, kept in an output the
// node never writes again ("Coeff") and in state.
[Parameter("Gain")]
const float32 gain = 2.0;

[Input("In")]
float32 input = 1.0;

[Output("Out")]
float32 output = 0.0;
[Output("Coeff")]
float32 coeff = 0.0;

float32 scaled = 0.0;

[NodeReset]
void Reset() {
    coeff = gain * 10.0;
    scaled = gain;
}

[NodeProcess]
void Process() {
    output = input * scaled;
}
