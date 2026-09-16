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