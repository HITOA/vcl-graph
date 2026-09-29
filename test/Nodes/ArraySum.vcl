// Reads an aggregate input, and passes it to a (read-only by default) parameter.
[Input("In")]
Array<float32, 4> values = { 1.0, 2.0, 3.0, 4.0 };

[Output("Out")]
float32 output = 0.0;

float32 Sum(Array<float32, 4> a) {
    return a[0] + a[1] + a[2] + a[3];
}

[NodeProcess]
void Process() {
    output = Sum(values) + values[3];
}
