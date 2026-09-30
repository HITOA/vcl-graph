// A template of the node using state: two specializations, one name.
[Input("In")]
float32 input = 0.0;
[Output("Out")]
float32 output = 0.0;

float32 sum = 0.0;

template<typename T>
T Add(T x) {
    sum += (float32)x;
    return (T)sum;
}

[NodeProcess]
void Process() {
    output = Add<float32>(input) + (float32)Add<int32>(1);
}
