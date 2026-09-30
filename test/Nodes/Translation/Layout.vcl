// State with scalar, vector and array fields.
[Output("Out")]
float32 output = 0.0;

uint8 flag = 1;
Vec<float32> v;
Array<int32, 5> values;
float64 d = 0.5;

[NodeProcess]
void Process() {
    v = v + 1.0;
    values[0] += 1;
    flag = 0;
    d += 1.0;
    output = (float32)d + (float32)values[0];
}
