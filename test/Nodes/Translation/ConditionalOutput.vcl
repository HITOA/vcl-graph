// The output is written only on some calls: it must persist.
[Input("In")]
float32 input = 0.0;
[Output("Out")]
float32 output = 0.0;

[NodeProcess]
void Process() {
    if (input > 0.5)
        output = input;
}
