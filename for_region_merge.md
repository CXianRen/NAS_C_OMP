```c

#pragma omp paralle
{
    // A
    #pragma omp for 


    // B
    #pragma omp for nowait

    // C
    #pragma omp for

    // D
    #pragma omp for nowait
}


// 插入 

#pragma omp paralle
{
    // A
    start()
    #pragma omp for 
    end()


    /* B and C is merged into a single region */
    // B
    start()
    #pragma omp for nowait

    // C
    #pragma omp for
    end()


    // D
    start()
    #pragma omp for nowait
}
end() // for D


```

显式 `barrier` 或有隐式 barrier 的 `single` 也可结束前面的 nowait 组，不新增同步点。

条件块或 helper 中的 nowait 遵守同一规则：编译期将成员分到固定组，END 插在已确定的已有 barrier 或 parallel join 后。生成的 master 标记只防止重复 START，并使全部分支跳过时不产生计时；它不在运行时判断或合并 region。helper 被多次调用但到同一个 barrier 才同步时，累计为一个区间。无法确定唯一终点或跨越条件 barrier 时，生成器报错。
