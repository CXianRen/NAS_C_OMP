make: Nothing to be done for 'binding_demo'.

=== close ===
OMP_NUM_THREADS=8  OMP_PLACES=cores  OMP_PROC_BIND=close
team_size=8  num_places=64  proc_bind_enabled=yes
tid             0	1	2	3	4	5	6	7
cpu             0	1	2	3	4	5	6	7
place           0	1	2	3	4	5	6	7
place_cpus      {0}	{1}	{2}	{3}	{4}	{5}	{6}	{7}
allowed_cpus    {0}	{1}	{2}	{3}	{4}	{5}	{6}	{7}

=== spread ===
OMP_NUM_THREADS=8  OMP_PLACES=cores  OMP_PROC_BIND=spread
team_size=8  num_places=64  proc_bind_enabled=yes
tid             0	1	2	3	4	5	6	7
cpu             0	8	16	24	32	40	48	56
place           0	8	16	24	32	40	48	56
place_cpus      {0}	{8}	{16}	{24}	{32}	{40}	{48}	{56}
allowed_cpus    {0}	{8}	{16}	{24}	{32}	{40}	{48}	{56}
