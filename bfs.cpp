#include <algorithm>
#include <chrono>
#include <cilk/cilk.h>
#include <ctime>
#include <functional>
#include <iostream>
#include <random>
#include <sys/time.h>
#include <vector>
#include <queue>
#include <atomic>
#include <cassert>
#include <memory>


using namespace std;


using std::chrono::duration_cast;
using std::chrono::milliseconds;
using std::chrono::seconds;
using std::chrono::system_clock;

constexpr int N = 500;
constexpr int BLOCK_SIZE = 250;
std::vector <std::vector<int>> arr(N * N * N);


int dx[] = {-1, 0, 0, 1, 0, 0};
int dy[] = {0, -1, 0, 0, 1, 0};
int dz[] = {0, 0, -1, 0, 0, 1};


struct Point {
    int x, y, z;
};

Point get_point(int index) {
    return {index % N, index / N % N, index / (N * N)};
}

int get_index(Point p) {
    return p.x + p.y * N + p.z * N * N;
}

void down(vector<int> &a, int l, int r) {
    if (r - l == 1) {
        return;
    }
    auto m = (l + r) / 2;

    cilk_scope{
            cilk_spawn down(a, l, m);
            down(a, m, r);
    }

    a[r - 1] += a[m - 1];
}

void up(vector<int> &a, int l, int r) {
    if (r - l == 1) {
        return;
    }
    auto m = (l + r) / 2;
    auto l_sum = a[m - 1];
    auto r_sum = a[r - 1];
    a[r - 1] = l_sum + r_sum;
    a[m - 1] = r_sum;

    cilk_scope{
            cilk_spawn up(a, l, m);
            up(a, m, r);
    }
}

/*
 * return last_element
 */
int scan(vector<int> &a) {
    int ans = a.back();
    down(a, 0, a.size());
    a.back() = 0;
    up(a, 0, a.size());
    return ans;
}

/*
 * return new_size
 */
int filter(vector<int> &a, int size) {

    auto blocked_size = size / BLOCK_SIZE + 1;

    vector<int> sums(blocked_size);

    cilk_for (int i = 0; i < blocked_size; i++) {
        for (int j = i * BLOCK_SIZE; j < min(size, (i + 1) * BLOCK_SIZE); j++) {
            sums[i] += (a[j] >= 0);
        }
    }

    int real_size = scan(sums) + sums.back();

    vector<int> ans(real_size, -1);

    cilk_for (int i = 0; i < blocked_size; i++) {
        int cnt = 0;
        for (int j = i * BLOCK_SIZE; j < min<int>(size, (i + 1) * BLOCK_SIZE); j++) {
            if (a[j] >= 0) {
                ans[sums[i] + cnt++] = a[j];
            }
        }
    }

    cilk_for (int i = 0; i < blocked_size; i++) {
        for (int j = i * BLOCK_SIZE; j < min<int>(ans.size(), (i + 1) * BLOCK_SIZE); j++) {
            a[j] = ans[j];
        }
    }

    return real_size;

}

int find_max_deg(vector<int>& a, int l, int r) {
    int ans = 0;
    if (r - l < BLOCK_SIZE) {
        for (int i = l; i < r; i++) {
            ans = std::max<int>(ans, arr[a[i]].size());
        }
    } else {
        auto m = (l + r) / 2;

        int mx1;
        int mx2;
        cilk_scope{
            mx1 = cilk_spawn find_max_deg(a, l, m);
            mx2 = find_max_deg(a, m, r);
        }

        ans = std::max(mx1, mx2);
    }

    return ans;

}


std::vector<int> par_bfs() {

    vector <vector<int>> f(2, vector<int>(N, -1));
    unique_ptr<atomic<bool>> visited(new atomic<bool>[N * N * N]);
    vector<int> ans(N * N * N, 0);
    vector<int> parents(N * N * N);

    visited.get()[0].store(true);

    int iteration = 0;
    f[iteration][0] = 0;
    int cnt_prev = 1;


    while (true) {
        cnt_prev = filter(f[iteration % 2], cnt_prev);

        if (cnt_prev == 0) {
            break;
        }


        int max_deg = find_max_deg(f[iteration % 2], 0, cnt_prev);


        f[(iteration + 1) % 2] = vector<int>(cnt_prev * max_deg, -1);

        auto blocked_size = cnt_prev / BLOCK_SIZE + 1;

        cilk_for (int z = 0; z < blocked_size; z++) {

            for (int i = z * BLOCK_SIZE; i < min<int>(cnt_prev, (z + 1) * BLOCK_SIZE); i++) {

                auto x = f[iteration % 2][i];
                int start_pos = i * max_deg;
                int cnt_cur = 0;

                for (auto j: arr[x]) {
                    auto last_visited = visited.get()[j].load(memory_order_relaxed);
                    if (!last_visited &&
                            visited.get()[j].compare_exchange_weak(last_visited, true, memory_order_relaxed)) {

                        f[(iteration + 1) % 2][start_pos + cnt_cur++] = j;
                        ans[j] = ans[x] + 1;
                        parents[j] = x;
                    }
                }

            }
        }

        cnt_prev = cnt_prev * max_deg;

        iteration++;

    }


    return ans;
}

std::vector<int> seq_bfs() {
    queue<int> q;
    std::vector<int> ans(N * N * N, -1);
    std::vector<int> parents(N * N * N, -1);
    std::vector<bool> visited(N * N * N, false);

    const int start = 0;

    q.push(start);
    parents[start] = 0;
    visited[start] = true;

    while (!q.empty()) {
        auto x = q.front();
        q.pop();

        ans[x] = ans[parents[x]] + 1;

        for (auto j: arr[x]) {
            if (!visited[j]) {
                visited[j] = true;
                parents[j] = x;
                q.push(j);
            }
        }

    }

    return ans;
}

size_t test(std::function<std::vector<int>()> f) {
    std::random_device dev;
    std::mt19937 rng(dev());

    size_t it_count = 5;

    size_t sum = 0;

    for (int i = 0; i < it_count; i++) {

        size_t start =
                duration_cast<milliseconds>(system_clock::now().time_since_epoch())
                        .count();

        auto ans = f();

        sum += duration_cast<milliseconds>(system_clock::now().time_since_epoch())
                       .count() - start;

        bool pass = true;
        for (int i = 0; i < N * N * N; i++) {
            auto p = get_point(i);
            if (ans[i] != p.x + p.y + p.z) {
                pass = false;
                break;
            }
        }


        std::cout << "BFS " << ((pass) ? "succeeded" : "failed") << "\n";

        cout << sum << endl;

    }

    return sum;
}


int main() {

    cilk_for (int i = 0; i < N * N * N; i++) {
        Point t = get_point(i);
        for (int j = 0; j < 6; j++) {
            Point p{t.x + dx[j], t.y + dy[j], t.z + dz[j]};

            if (p.x >= 0 && p.x < N && p.y >= 0 && p.y < N && p.z >= 0 && p.z < N) {

                arr[i].push_back(get_index(p));
            }
        }
    }


    size_t seq = test(seq_bfs);
    size_t par = test(par_bfs);

    cout << "par bfs: " << par << endl;
    cout << "seq bfs: " << seq << endl;
    cout << "acceleration: " << (long double) (seq) / par << "x" << endl;

}
