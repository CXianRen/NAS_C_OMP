# NAS region 耗时占比

问题规模：Class B (compiled problem size; reduced iteration count)。

环境：Intel(R) Core(TM) Ultra 5 235U；threads=8；OMP_PLACES=cores；OMP_PROC_BIND=close；OMP_DYNAMIC=false。

迭代设置：BT/LU/CG/SP/MG/FT: NPB_NITER=3; UA: 6 steps, adaptation frequency 5, 10 inner CG steps; EP/IS: default B workload。

范围：BT CG EP FT IS LU MG SP UA。

[完整运行配置与命令](metadata.json)。

占总时间（平均 time step 占比）= region 累计耗时 / 正式 iteration 累计耗时。父级比例：parallel / iteration total，for / 所属 parallel。

本次缩短迭代的条目（见上方 iterations）不再对应标准迭代次数的参考结果。其 UNSUCCESSFUL 或 Class U 按原日志照录，不据此判定数值回归；EP / IS 保留完整 Class B 工作量，仍按标准参考结果验证。

DC 不含本次所需的 iteration / omp for 层级，本次不运行。

| Benchmark | iteration 总时间 (s) | 数值验证 | 最大 parallel（占总时间） | 最大 for（占总时间） |
| --- | ---: | --- | --- | --- |
| BT | 0.543 | UNSUCCESSFUL | z_solve:71 (29.907%) | z_solve:71 (29.907%) |
| SP | 0.201 | UNSUCCESSFUL | compute_rhs:45 (39.537%) | z_solve:54 (15.699%) |
| LU | 0.257 | UNSUCCESSFUL | ssor:137 (69.763%) | buts:68-207 (nowait) (13.842%) |
| CG | 0.309 | UNSUCCESSFUL | conj_grad:443 (99.953%) | conj_grad:500 (94.809%) |
| MG | 0.166 | UNSUCCESSFUL | resid:545 (52.642%) | resid:545 (52.642%) |
| FT | 0.623 | UNSUCCESSFUL | cffts1:443 (30.453%) | cffts1:443 (30.453%) |
| EP | 8.931 | SUCCESSFUL | main:173 (100.000%) | main:180-227 (nowait) (95.294%) |
| IS | 0.214 | SUCCESSFUL | rank:630 (99.999%) | rank:676 (65.518%) |
| UA | 0.669 | UNSUCCESSFUL | transfb:288 (34.712%) | transfb:300-575 (nowait) (28.363%) |

parallel 与 for 的时间有重叠；combined parallel for 在两层显示同一份耗时，不能相加。
nowait 名称中的起止行号对应首个 nowait pragma 与现有计时结束标记，连续 nowait 组仍累计为一项。
只由 master 读钟；nowait 组在既定代码边界结束，不增加同步，因此不代表所有线程完成该组的耗时。

## BT

原始日志：[BT.log](BT.log)。总时间优先取 `iteration total`，旧日志取 `total`。日志 Class: `U`；数值验证: `UNSUCCESSFUL`。

| 层级 | Region | 父级 | 累计耗时 (s) | 占总时间 | 占父级 |
| --- | --- | --- | ---: | ---: | ---: |
| parallel | add:47 | iteration total | 0.013307141 | 2.451% | 2.451% |
| for | add:47 | parallel add:47 | 0.013307141 | 2.451% | 100.000% |
| parallel | compute_rhs:45 | iteration total | 0.107457475 | 19.795% | 19.795% |
| for | compute_rhs:53-76 (nowait) | parallel compute_rhs:45 | 0.007899493 | 1.455% | 7.351% |
| for | compute_rhs:78 | parallel compute_rhs:45 | 0.016785725 | 3.092% | 15.621% |
| for | compute_rhs:96-207 (nowait) | parallel compute_rhs:45 | 0.014575908 | 2.685% | 13.564% |
| for | compute_rhs:209 | parallel compute_rhs:45 | 0.016490223 | 3.038% | 15.346% |
| for | compute_rhs:321 | parallel compute_rhs:45 | 0.025896107 | 4.770% | 24.099% |
| for | compute_rhs:380-430 (nowait) | parallel compute_rhs:45 | 0.011379830 | 2.096% | 10.590% |
| for | compute_rhs:432 | parallel compute_rhs:45 | 0.006358770 | 1.171% | 5.917% |
| for | compute_rhs:447-457 (nowait) | parallel compute_rhs:45 | 0.005260357 | 0.969% | 4.895% |
| parallel | x_solve:72 | iteration total | 0.119209548 | 21.960% | 21.960% |
| for | x_solve:72 | parallel x_solve:72 | 0.119209548 | 21.960% | 100.000% |
| parallel | y_solve:71 | iteration total | 0.140515145 | 25.884% | 25.884% |
| for | y_solve:71 | parallel y_solve:71 | 0.140515145 | 25.884% | 100.000% |
| parallel | z_solve:71 | iteration total | 0.162351944 | 29.907% | 29.907% |
| for | z_solve:71 | parallel z_solve:71 | 0.162351944 | 29.907% | 100.000% |

## SP

原始日志：[SP.log](SP.log)。总时间优先取 `iteration total`，旧日志取 `total`。日志 Class: `U`；数值验证: `UNSUCCESSFUL`。

| 层级 | Region | 父级 | 累计耗时 (s) | 占总时间 | 占父级 |
| --- | --- | --- | ---: | ---: | ---: |
| parallel | add:46 | iteration total | 0.006742645 | 3.362% | 3.362% |
| for | add:46 | parallel add:46 | 0.006742645 | 3.362% | 100.000% |
| parallel | ninvr:47 | iteration total | 0.004580416 | 2.284% | 2.284% |
| for | ninvr:47 | parallel ninvr:47 | 0.004580416 | 2.284% | 100.000% |
| parallel | pinvr:47 | iteration total | 0.004267307 | 2.128% | 2.128% |
| for | pinvr:47 | parallel pinvr:47 | 0.004267307 | 2.128% | 100.000% |
| parallel | compute_rhs:45 | iteration total | 0.079298768 | 39.537% | 39.537% |
| for | compute_rhs:53-81 (nowait) | parallel compute_rhs:45 | 0.009518808 | 4.746% | 12.004% |
| for | compute_rhs:83 | parallel compute_rhs:45 | 0.006285944 | 3.134% | 7.927% |
| for | compute_rhs:101-195 (nowait) | parallel compute_rhs:45 | 0.014650503 | 7.304% | 18.475% |
| for | compute_rhs:197 | parallel compute_rhs:45 | 0.013103404 | 6.533% | 16.524% |
| for | compute_rhs:297 | parallel compute_rhs:45 | 0.018311177 | 9.130% | 23.091% |
| for | compute_rhs:345-394 (nowait) | parallel compute_rhs:45 | 0.010584612 | 5.277% | 13.348% |
| for | compute_rhs:396 | parallel compute_rhs:45 | 0.002585945 | 1.289% | 3.261% |
| for | compute_rhs:410-420 (nowait) | parallel compute_rhs:45 | 0.004025032 | 2.007% | 5.076% |
| parallel | txinvr:47 | iteration total | 0.007607743 | 3.793% | 3.793% |
| for | txinvr:47 | parallel txinvr:47 | 0.007607743 | 3.793% | 100.000% |
| parallel | tzetar:48 | iteration total | 0.009983587 | 4.978% | 4.978% |
| for | tzetar:48 | parallel tzetar:48 | 0.009983587 | 4.978% | 100.000% |
| parallel | x_solve:50 | iteration total | 0.027953147 | 13.937% | 13.937% |
| for | x_solve:50 | parallel x_solve:50 | 0.027953147 | 13.937% | 100.000% |
| parallel | y_solve:50 | iteration total | 0.028621441 | 14.270% | 14.270% |
| for | y_solve:50 | parallel y_solve:50 | 0.028621441 | 14.270% | 100.000% |
| parallel | z_solve:54 | iteration total | 0.031487252 | 15.699% | 15.699% |
| for | z_solve:54 | parallel z_solve:54 | 0.031487252 | 15.699% | 100.000% |

## LU

原始日志：[LU.log](LU.log)。总时间优先取 `iteration total`，旧日志取 `total`。日志 Class: `U`；数值验证: `UNSUCCESSFUL`。

| 层级 | Region | 父级 | 累计耗时 (s) | 占总时间 | 占父级 |
| --- | --- | --- | ---: | ---: | ---: |
| parallel | l2norm:60 | iteration total | 0.000844134 | 0.329% | 0.329% |
| for | l2norm:66-76 (nowait) | parallel l2norm:60 | 0.000836194 | 0.326% | 99.059% |
| parallel | rhs:59 | iteration total | 0.076757121 | 29.887% | 29.887% |
| for | rhs:65 | parallel rhs:59 | 0.012253314 | 4.771% | 15.964% |
| for | rhs:89-214 (nowait) | parallel rhs:59 | 0.011997172 | 4.671% | 15.630% |
| for | rhs:216 | parallel rhs:59 | 0.025544240 | 9.946% | 33.279% |
| for | rhs:351-473 (nowait) | parallel rhs:59 | 0.026345210 | 10.258% | 34.323% |
| parallel | ssor:137 | iteration total | 0.179166952 | 69.763% | 69.763% |
| for | blts:70-207 (nowait) | parallel ssor:137 | 0.029877284 | 11.634% | 16.676% |
| for | buts:68-207 (nowait) | parallel ssor:137 | 0.035549309 | 13.842% | 19.841% |
| for | jacld:56-339 (nowait) | parallel ssor:137 | 0.024657816 | 9.601% | 13.762% |
| for | jacu:56-359 (nowait) | parallel ssor:137 | 0.025568252 | 9.956% | 14.271% |
| for | ssor:144-163 (nowait) | parallel ssor:137 | 0.003693734 | 1.438% | 2.062% |
| for | ssor:229-239 (nowait) | parallel ssor:137 | 0.006522638 | 2.540% | 3.641% |

## CG

原始日志：[CG.log](CG.log)。总时间优先取 `iteration total`，旧日志取 `benchmk`。日志 Class: `B`；数值验证: `UNSUCCESSFUL`。

| 层级 | Region | 父级 | 累计耗时 (s) | 占总时间 | 占父级 |
| --- | --- | --- | ---: | ---: | ---: |
| parallel | main:343 | iteration total | 0.000054019 | 0.017% | 0.017% |
| for | main:343 | parallel main:343 | 0.000054019 | 0.017% | 100.000% |
| parallel | main:362 | iteration total | 0.000018900 | 0.006% | 0.006% |
| for | main:362 | parallel main:362 | 0.000018900 | 0.006% | 100.000% |
| parallel | conj_grad:443 | iteration total | 0.308848688 | 99.953% | 99.953% |
| for | conj_grad:450 | parallel conj_grad:443 | 0.000110980 | 0.036% | 0.036% |
| for | conj_grad:464 | parallel conj_grad:443 | 0.000026844 | 0.009% | 0.009% |
| for | conj_grad:500 | parallel conj_grad:443 | 0.292953561 | 94.809% | 94.853% |
| for | conj_grad:552 | parallel conj_grad:443 | 0.001103848 | 0.357% | 0.357% |
| for | conj_grad:568 | parallel conj_grad:443 | 0.001968131 | 0.637% | 0.637% |
| for | conj_grad:590 | parallel conj_grad:443 | 0.001293983 | 0.419% | 0.419% |
| for | conj_grad:603 | parallel conj_grad:443 | 0.011121402 | 3.599% | 3.601% |
| for | conj_grad:617-622 (nowait) | parallel conj_grad:443 | 0.000046038 | 0.015% | 0.015% |

## MG

原始日志：[MG.log](MG.log)。总时间优先取 `iteration total`，旧日志取 `benchmk`。日志 Class: `B`；数值验证: `UNSUCCESSFUL`。

| 层级 | Region | 父级 | 累计耗时 (s) | 占总时间 | 占父级 |
| --- | --- | --- | ---: | ---: | ---: |
| parallel | psinv:480 | iteration total | 0.037292193 | 22.478% | 22.478% |
| for | psinv:480 | parallel psinv:480 | 0.037292193 | 22.478% | 100.000% |
| parallel | resid:545 | iteration total | 0.087335639 | 52.642% | 52.642% |
| for | resid:545 | parallel resid:545 | 0.087335639 | 52.642% | 100.000% |
| parallel | rprj3:625 | iteration total | 0.015779422 | 9.511% | 9.511% |
| for | rprj3:625 | parallel rprj3:625 | 0.015779422 | 9.511% | 100.000% |
| parallel | interp:697 | iteration total | 0.018763791 | 11.310% | 11.310% |
| for | interp:697 | parallel interp:697 | 0.018763791 | 11.310% | 100.000% |
| parallel | comm3:920 | iteration total | 0.005060806 | 3.050% | 3.050% |
| for | comm3:923 | parallel comm3:920 | 0.003538883 | 2.133% | 69.927% |
| for | comm3:940-947 (nowait) | parallel comm3:920 | 0.000537315 | 0.324% | 10.617% |
| parallel | zero3:1297 | iteration total | 0.001609642 | 0.970% | 0.970% |
| for | zero3:1297 | parallel zero3:1297 | 0.001609642 | 0.970% | 100.000% |

## FT

原始日志：[FT.log](FT.log)。总时间优先取 `iteration total`，旧日志取 `total`。日志 Class: `U`；数值验证: `UNSUCCESSFUL`。

| 层级 | Region | 父级 | 累计耗时 (s) | 占总时间 | 占父级 |
| --- | --- | --- | ---: | ---: | ---: |
| parallel | evolve:226 | iteration total | 0.124074186 | 19.916% | 19.916% |
| for | evolve:226 | parallel evolve:226 | 0.124074186 | 19.916% | 100.000% |
| parallel | cffts1:443 | iteration total | 0.189716741 | 30.453% | 30.453% |
| for | cffts1:443 | parallel cffts1:443 | 0.189716741 | 30.453% | 100.000% |
| parallel | cffts2:478 | iteration total | 0.150081801 | 24.091% | 24.091% |
| for | cffts2:478 | parallel cffts2:478 | 0.150081801 | 24.091% | 100.000% |
| parallel | cffts3:513 | iteration total | 0.158813318 | 25.492% | 25.492% |
| for | cffts3:513 | parallel cffts3:513 | 0.158813318 | 25.492% | 100.000% |
| parallel | checksum:678 | iteration total | 0.000258576 | 0.042% | 0.042% |
| for | checksum:683-690 (nowait) | parallel checksum:678 | 0.000049625 | 0.008% | 19.192% |

## EP

原始日志：[EP.log](EP.log)。总时间优先取 `iteration total`，旧日志取 `Total time`。日志 Class: `B`；数值验证: `SUCCESSFUL`。

| 层级 | Region | 父级 | 累计耗时 (s) | 占总时间 | 占父级 |
| --- | --- | --- | ---: | ---: | ---: |
| parallel | main:173 | iteration total | 8.931336211 | 100.000% | 100.000% |
| for | main:180-227 (nowait) | parallel main:173 | 8.511058218 | 95.294% | 95.294% |

## IS

原始日志：[IS.log](IS.log)。总时间优先取 `iteration total`，旧日志取 `Benchmarking`。日志 Class: `B`；数值验证: `SUCCESSFUL`。

| 层级 | Region | 父级 | 累计耗时 (s) | 占总时间 | 占父级 |
| --- | --- | --- | ---: | ---: | ---: |
| parallel | rank:630 | iteration total | 0.213852136 | 99.999% | 99.999% |
| for | rank:654 | parallel rank:630 | 0.038144737 | 17.837% | 17.837% |
| for | rank:676 | parallel rank:630 | 0.140113764 | 65.518% | 65.519% |
| for | rank:702 | parallel rank:630 | 0.035381022 | 16.544% | 16.545% |

## UA

原始日志：[UA.log](UA.log)。总时间优先取 `iteration total`，旧日志取 `total`。日志 Class: `U`；数值验证: `UNSUCCESSFUL`。

| 层级 | Region | 父级 | 累计耗时 (s) | 占总时间 | 占父级 |
| --- | --- | --- | ---: | ---: | ---: |
| parallel | adaptation:113 | iteration total | 0.000002219 | 0.000% | 0.000% |
| for | adaptation:113 | parallel adaptation:113 | 0.000002219 | 0.000% | 100.000% |
| parallel | adaptation:151 | iteration total | 0.000018790 | 0.003% | 0.003% |
| for | adaptation:151 | parallel adaptation:151 | 0.000018790 | 0.003% | 100.000% |
| parallel | do_coarsen:192 | iteration total | 0.000011335 | 0.002% | 0.002% |
| for | do_coarsen:195-204 (nowait) | parallel do_coarsen:192 | 0.000003245 | 0.000% | 28.628% |
| parallel | do_coarsen:211 | iteration total | 0.000120721 | 0.018% | 0.018% |
| for | do_coarsen:211 | parallel do_coarsen:211 | 0.000120721 | 0.018% | 100.000% |
| parallel | do_coarsen:296 | iteration total | 0.000014604 | 0.002% | 0.002% |
| for | do_coarsen:296 | parallel do_coarsen:296 | 0.000014604 | 0.002% | 100.000% |
| parallel | do_coarsen:313 | iteration total | 0.000112243 | 0.017% | 0.017% |
| for | do_coarsen:313 | parallel do_coarsen:313 | 0.000112243 | 0.017% | 100.000% |
| parallel | do_refine:352 | iteration total | 0.000009403 | 0.001% | 0.001% |
| for | do_refine:352 | parallel do_refine:352 | 0.000009403 | 0.001% | 100.000% |
| parallel | do_refine:374 | iteration total | 0.000003890 | 0.001% | 0.001% |
| for | do_refine:374 | parallel do_refine:374 | 0.000003890 | 0.001% | 100.000% |
| parallel | do_refine:387 | iteration total | 0.000006850 | 0.001% | 0.001% |
| for | do_refine:387 | parallel do_refine:387 | 0.000006850 | 0.001% | 100.000% |
| parallel | do_refine:416 | iteration total | 0.000319818 | 0.048% | 0.048% |
| for | do_refine:416 | parallel do_refine:416 | 0.000319818 | 0.048% | 100.000% |
| parallel | find_coarsen:758 | iteration total | 0.000073356 | 0.011% | 0.011% |
| for | find_coarsen:758 | parallel find_coarsen:758 | 0.000073356 | 0.011% | 100.000% |
| parallel | find_refine:794 | iteration total | 0.000105556 | 0.016% | 0.016% |
| for | find_refine:794 | parallel find_refine:794 | 0.000105556 | 0.016% | 100.000% |
| parallel | check_refine:821 | iteration total | 0.000019193 | 0.003% | 0.003% |
| for | check_refine:821 | parallel check_refine:821 | 0.000019193 | 0.003% | 100.000% |
| parallel | convect:72 | iteration total | 0.074238345 | 11.097% | 11.097% |
| for | convect:72 | parallel convect:72 | 0.074238345 | 11.097% | 100.000% |
| parallel | convect:249 | iteration total | 0.000732711 | 0.110% | 0.110% |
| for | convect:249 | parallel convect:249 | 0.000732711 | 0.110% | 100.000% |
| parallel | diffusion:57 | iteration total | 0.000430883 | 0.064% | 0.064% |
| for | diffusion:57 | parallel diffusion:57 | 0.000430883 | 0.064% | 100.000% |
| parallel | diffusion:70 | iteration total | 0.005854300 | 0.875% | 0.875% |
| for | diffusion:73-91 (nowait) | parallel diffusion:70 | 0.005397478 | 0.807% | 92.197% |
| parallel | diffusion:102 | iteration total | 0.049169295 | 7.350% | 7.350% |
| for | diffusion:108-126 (nowait) | parallel diffusion:102 | 0.041500287 | 6.203% | 84.403% |
| parallel | diffusion:147 | iteration total | 0.058138371 | 8.690% | 8.690% |
| for | diffusion:147 | parallel diffusion:147 | 0.058138371 | 8.690% | 100.000% |
| parallel | diffusion:161 | iteration total | 0.000974293 | 0.146% | 0.146% |
| for | diffusion:161 | parallel diffusion:161 | 0.000974293 | 0.146% | 100.000% |
| parallel | diffusion:174 | iteration total | 0.036063284 | 5.391% | 5.391% |
| for | diffusion:178-195 (nowait) | parallel diffusion:174 | 0.030502007 | 4.559% | 84.579% |
| parallel | mortar:82 | iteration total | 0.000059450 | 0.009% | 0.009% |
| for | mortar:82 | parallel mortar:82 | 0.000059450 | 0.009% | 100.000% |
| parallel | mortar:754 | iteration total | 0.000003865 | 0.001% | 0.001% |
| for | mortar:754 | parallel mortar:754 | 0.000003865 | 0.001% | 100.000% |
| parallel | mortar:768 | iteration total | 0.000519054 | 0.078% | 0.078% |
| for | mortar:768 | parallel mortar:768 | 0.000519054 | 0.078% | 100.000% |
| parallel | mortar:817 | iteration total | 0.000054474 | 0.008% | 0.008% |
| for | mortar:817 | parallel mortar:817 | 0.000054474 | 0.008% | 100.000% |
| parallel | mortar:1362 | iteration total | 0.000002136 | 0.000% | 0.000% |
| for | mortar:1362 | parallel mortar:1362 | 0.000002136 | 0.000% | 100.000% |
| parallel | mortar:1377 | iteration total | 0.000274057 | 0.041% | 0.041% |
| for | mortar:1377 | parallel mortar:1377 | 0.000274057 | 0.041% | 100.000% |
| parallel | mortar:1497 | iteration total | 0.000128023 | 0.019% | 0.019% |
| for | mortar:1497 | parallel mortar:1497 | 0.000128023 | 0.019% | 100.000% |
| parallel | move:51 | iteration total | 0.000996275 | 0.149% | 0.149% |
| for | move:55 | parallel move:51 | 0.000525072 | 0.078% | 52.704% |
| for | move:106 | parallel move:51 | 0.000455547 | 0.068% | 45.725% |
| parallel | setuppc:62 | iteration total | 0.000711270 | 0.106% | 0.106% |
| for | setuppc:62 | parallel setuppc:62 | 0.000711270 | 0.106% | 100.000% |
| parallel | setuppc:92 | iteration total | 0.000138344 | 0.021% | 0.021% |
| for | setuppc:92 | parallel setuppc:92 | 0.000138344 | 0.021% | 100.000% |
| parallel | setpcmo:448 | iteration total | 0.000022617 | 0.003% | 0.003% |
| for | setpcmo:451-455 (nowait) | parallel setpcmo:448 | 0.000001017 | 0.000% | 4.497% |
| for | setpcmo:458 | parallel setpcmo:448 | 0.000014863 | 0.002% | 65.716% |
| parallel | setpcmo:471 | iteration total | 0.000181546 | 0.027% | 0.027% |
| for | setpcmo:471 | parallel setpcmo:471 | 0.000181546 | 0.027% | 100.000% |
| parallel | prepwork:318 | iteration total | 0.000374543 | 0.056% | 0.056% |
| for | prepwork:323-336 (nowait) | parallel prepwork:318 | 0.000215599 | 0.032% | 57.563% |
| for | prepwork:340 | parallel prepwork:318 | 0.000121143 | 0.018% | 32.344% |
| for | prepwork:347-415 (nowait) | parallel prepwork:318 | 0.000011172 | 0.002% | 2.983% |
| parallel | transf:67 | iteration total | 0.059956646 | 8.962% | 8.962% |
| for | transf:67 | parallel transf:67 | 0.059956646 | 8.962% | 100.000% |
| parallel | transfb:288 | iteration total | 0.232218501 | 34.712% | 34.712% |
| for | transfb:293 | parallel transfb:288 | 0.004873427 | 0.728% | 2.099% |
| for | transfb:300-575 (nowait) | parallel transfb:288 | 0.189746985 | 28.363% | 81.711% |
| parallel | transfb_c:852 | iteration total | 0.014814767 | 2.215% | 2.215% |
| for | transfb_c:856 | parallel transfb_c:852 | 0.000318467 | 0.048% | 2.150% |
| for | transfb_c:863-949 (nowait) | parallel transfb_c:852 | 0.012576693 | 1.880% | 84.893% |
| parallel | transfb_c_2:967 | iteration total | 0.003412984 | 0.510% | 0.510% |
| for | transfb_c_2:972-976 (nowait) | parallel transfb_c_2:967 | 0.000053736 | 0.008% | 1.574% |
| for | transfb_c_2:978 | parallel transfb_c_2:967 | 0.000097973 | 0.015% | 2.871% |
| for | transfb_c_2:985-1081 (nowait) | parallel transfb_c_2:967 | 0.002107219 | 0.315% | 61.741% |
| parallel | main:354 | iteration total | 0.007786859 | 1.164% | 1.164% |
| for | main:357 | parallel main:354 | 0.005605985 | 0.838% | 71.993% |
| for | main:365 | parallel main:354 | 0.002134673 | 0.319% | 27.414% |
| parallel | main:387 | iteration total | 0.000112860 | 0.017% | 0.017% |
| for | main:387 | parallel main:387 | 0.000112860 | 0.017% | 100.000% |
| parallel | reciprocal:45 | iteration total | 0.000358334 | 0.054% | 0.054% |
| for | reciprocal:45 | parallel reciprocal:45 | 0.000358334 | 0.054% | 100.000% |
| parallel | r_init_omp:61 | iteration total | 0.001083240 | 0.162% | 0.162% |
| for | r_init_omp:61 | parallel r_init_omp:61 | 0.001083240 | 0.162% | 100.000% |
| parallel | nr_init_omp:90 | iteration total | 0.000802206 | 0.120% | 0.120% |
| for | nr_init_omp:90 | parallel nr_init_omp:90 | 0.000802206 | 0.120% | 100.000% |
| parallel | l_init_omp:119 | iteration total | 0.000122540 | 0.018% | 0.018% |
| for | l_init_omp:119 | parallel l_init_omp:119 | 0.000122540 | 0.018% | 100.000% |
| parallel | adds2m1:173 | iteration total | 0.062930212 | 9.407% | 9.407% |
| for | adds2m1:173 | parallel adds2m1:173 | 0.062930212 | 9.407% | 100.000% |
| parallel | adds1m1:188 | iteration total | 0.024807282 | 3.708% | 3.708% |
| for | adds1m1:188 | parallel adds1m1:188 | 0.024807282 | 3.708% | 100.000% |
| parallel | col2:204 | iteration total | 0.027686922 | 4.139% | 4.139% |
| for | col2:204 | parallel col2:204 | 0.027686922 | 4.139% | 100.000% |
| parallel | add2:232 | iteration total | 0.002228545 | 0.333% | 0.333% |
| for | add2:232 | parallel add2:232 | 0.002228545 | 0.333% | 100.000% |
| parallel | parallel_add:291 | iteration total | 0.000332436 | 0.050% | 0.050% |
| for | parallel_add:291 | parallel parallel_add:291 | 0.000332436 | 0.050% | 100.000% |
| parallel | parallel_add:305 | iteration total | 0.000126611 | 0.019% | 0.019% |
| for | parallel_add:305 | parallel parallel_add:305 | 0.000126611 | 0.019% | 100.000% |
