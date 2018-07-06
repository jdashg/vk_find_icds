mkdir out 2>/dev/null
#args="-framework CoreFoundation"
$CXX --std=c++14 vk_find_icds.cpp vk_dump_icds.cpp -o out/vk_dump_icds $args $@
