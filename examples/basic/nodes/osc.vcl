/**
*   VCL SIN OSC
*/

in vfloat frame;
in vfloat freq;
in float rate;

out vfloat output;

[NodeProcess]
void Process() {
    output = sin(frame / rate / freq);
}