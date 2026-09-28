[Output("Values")]
Array<float32, 4> values;

[NodeProcess]
void Process() {
    for (uint32 i = 0; i < 4; ++i)
        values[i] = (float32)i;
}
