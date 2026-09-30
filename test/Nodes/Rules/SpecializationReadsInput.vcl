// Rule 1 in an explicit specialization of a template helper.
[Input("In")]
float32 input = 0.0;
[Output("Out")]
float32 output = 0.0;

template<typename T>
T Scale(T x) {
    return x * 2.0;
}

special<float32>
float32 Scale(float32 x) {
    return x * input;
}

[NodeProcess]
void Process() {
    output = Scale<float32>(input);
}
