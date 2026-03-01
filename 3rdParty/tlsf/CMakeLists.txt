cmake_minimum_required(VERSION 3.16)
project(tlsf)

add_library(tlsf tlsf.c)
target_include_directories(tlsf PUBLIC ./)