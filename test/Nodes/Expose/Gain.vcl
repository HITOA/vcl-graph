// A gain the UI may edit live.
[Input("In")]
float32 input = 1.0;

[Input("Gain"), Expose(Write)]
float32 gain = 2.0;

[Output("Out")]
float32 output;

[NodeProcess]
void Process() {
    output = input * gain;
}
