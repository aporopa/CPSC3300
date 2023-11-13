// Abigail Poropatich
// CPSC 3220: Operating Systems
// Project 3: CPU Scheduling
// 11 November 2023

#include <iostream>
#include <queue>
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>

using namespace std;

struct Task {
    char id;
    int arrival_time;
    int service_time;
    int remaining_time;
    int start_time;
    int completion_time;
    int wait_time;
    int response_time;

    Task(char id, int arrival, int service) 
    : id(id), arrival_time(arrival), service_time(service),
      remaining_time(service), start_time(-1), completion_time(0), wait_time(0), response_time(0) {}

};

void simulate_fifo(vector<Task>& tasks);
void simulate_sjf(vector<Task>& tasks);
void simulate_rr(vector<Task>& tasks);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " -fifo | -sjf | -rr\n";
        return 1;
    }

    string policy = argv[1];
    vector<Task> tasks;
    int arrival, service;
    char next_id = 'A';

    while (cin >> arrival >> service) {
        tasks.emplace_back(next_id++, arrival, service);
    }

    if (policy == "-fifo") {
        simulate_fifo(tasks);
    } else if (policy == "-sjf") {
        simulate_sjf(tasks);
    } else if (policy == "-rr") {
        simulate_rr(tasks);
    } else {
        cerr << "Invalid scheduling policy\n";
        return 1;
    }

    return 0;
}

void simulate_fifo(vector<Task>& tasks) {
    int time = 0;
    queue<Task*> task_queue;
    vector<Task>::iterator it = tasks.begin();

    Task* current_task = nullptr; 
    int start_time = 0;

    sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
        return a.arrival_time < b.arrival_time;
    });

    cout << "FIFO scheduling results\n\n";
    cout << "time   cpu   ready queue (tid/rst)\n";
    cout << "----   ---   ---------------------\n";

    for (auto& task : tasks) {
        task.wait_time = 0;
        task.response_time = -1;
    }

    // Driver loop for FIFO
    while(it != tasks.end() || !task_queue.empty() || current_task != nullptr) {
        // Add tasks to the queue if they have arrived
        while(it != tasks.end() && it->arrival_time <= time) {
            task_queue.push(&(*it));
            ++it;
        }

        // Fetching next task if CPU is idle
        if (!current_task && !task_queue.empty()) {
            current_task = task_queue.front();
            task_queue.pop();
            start_time = time; 

            // Assigning response time
            if (current_task->response_time == -1) { 
                current_task->response_time = (time - current_task->arrival_time) + (current_task->wait_time + current_task->service_time);
            }
        }

        // Start of printing and formatting
        cout << setw(3) << time;
        if (current_task) {
            cout << setw(5) << current_task->id << current_task->remaining_time;
        } else {
            cout << setw(10);
        }

        cout << "    ";
        if (task_queue.empty()) {
            cout << "--";
        } else {
            queue<Task*> temp_queue = task_queue;
            // Printing the ready queue
            while (!temp_queue.empty()) {
                Task* waiting_task = temp_queue.front();
                temp_queue.pop();
                cout << waiting_task->id << waiting_task->remaining_time;
                if (!temp_queue.empty()) cout << ",";
            }
        }
        cout << endl;

        // Processing the current task
        if (current_task) {
            current_task->remaining_time--;
            if (current_task->remaining_time == 0) { 
                current_task->completion_time = time + 1;
                current_task->wait_time = start_time - current_task->arrival_time;
                current_task = nullptr; 
            }
        }
        time++;
    }
    
    // Menu output
    cout << "\n     arrival service completion response wait";
    cout << "\ntid   time    time      time      time   time";
    cout << "\n---  ------- ------- ---------- -------- ----\n";
    for (const Task& task : tasks) {
        cout << " " << task.id << setw(7)
             << task.arrival_time << setw(8)
             << task.service_time << setw(10)
             << task.completion_time << setw(10)
             << task.response_time << setw(7)
             << task.wait_time << "\n";
    }

    cout << "\nservice wait\n time   time\n";
    cout << "------- ----\n";
    sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
        if (a.service_time == b.service_time) return a.arrival_time < b.arrival_time;
        return a.service_time < b.service_time;
    });
    for (const Task& task : tasks) {
        cout << setw(4) << task.service_time << "\t" << setw(3) << task.wait_time << "\n";
    }

}

void simulate_sjf(vector<Task>& tasks) {
    int time = 0;
    bool is_cpu_idle = true;
    Task* current_task = nullptr;

    // Comp compairs the remainng time of two tasks
    auto comp = [](const Task* a, const Task* b) {
        return a->remaining_time > b->remaining_time;
    };

    // Sorts tasks by remaining time using comp
    priority_queue<Task*, vector<Task*>, decltype(comp)> ready_queue(comp);

    // Sorting by arrival time
    sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
        return a.arrival_time < b.arrival_time;
    });

    for (auto& task : tasks) {
        task.wait_time = 0;
        task.response_time = -1;
    }

    cout << "SJF(preemptive) scheduling results\n\n";
    cout << "time   cpu   ready queue (tid/rst)\n";
    cout << "----   ---   ---------------------\n";

    // Driver loop for SJF
    vector<Task>::iterator it = tasks.begin();
    while (it != tasks.end() || !ready_queue.empty() || current_task != nullptr) {
        while (it != tasks.end() && it->arrival_time <= time) {
            if (is_cpu_idle || (current_task != nullptr && it->service_time < current_task->remaining_time)) {
                if (current_task != nullptr) {
                ready_queue.push(current_task);
                current_task = nullptr;
            }
                current_task = &(*it);
                is_cpu_idle = false;
            } else {
                ready_queue.push(&(*it));
            }
            ++it;
        }

        // If CPU is idle and other tasks are waiting then fetch the next task
        if (is_cpu_idle && !ready_queue.empty()) {
            current_task = ready_queue.top();
            ready_queue.pop();
            is_cpu_idle = false;
            
        }

        cout << setw(3) << time;
        if (!is_cpu_idle) {
            cout << setw(5) << current_task->id << current_task->remaining_time;
            current_task->remaining_time--;

            // Check if task is completed
            if (current_task->remaining_time <= 0) {
                current_task->completion_time = time + 1;
                if (current_task->response_time == -1) {
                    current_task->response_time = current_task->completion_time - current_task->arrival_time;
                }
                current_task = nullptr;
                is_cpu_idle = true;
            }
        } else {
            cout << setw(10);
        }

        cout << "    ";
        if (ready_queue.empty()) {

            cout << "--";
        } else {
            vector<Task*> tasks_in_queue;
            priority_queue<Task*, vector<Task*>, decltype(comp)> tempQueue = ready_queue;

            while (!tempQueue.empty()) {
                Task* task = tempQueue.top();
                tempQueue.pop();
                tasks_in_queue.push_back(task);
            }

            sort(tasks_in_queue.begin(), tasks_in_queue.end(), [](const Task* a, const Task* b) {
                return a->remaining_time < b->remaining_time; 
            });

            for (Task* task : tasks_in_queue) {
                cout << task->id << task->remaining_time;
                if(task != tasks_in_queue.back()) cout << ", ";
            }

        } 
        cout << endl;

        time++;
    }

    for (auto& task : tasks) {
        task.wait_time = task.completion_time - task.arrival_time - task.service_time;
    }

    // Menu output
    cout << "\n     arrival service completion response wait";
    cout << "\ntid   time    time      time      time   time";
    cout << "\n---  ------- ------- ---------- -------- ----\n";
    for (const Task& task : tasks) {
        cout << " " << task.id << setw(7)
             << task.arrival_time << setw(8)
             << task.service_time << setw(10)
             << task.completion_time << setw(10)
             << task.response_time << setw(7)
             << task.wait_time << "\n";
    }

    cout << "\nservice wait\n time   time\n";
    cout << "------- ----\n";
    sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
        if (a.service_time == b.service_time) return a.arrival_time < b.arrival_time;
        return a.service_time < b.service_time;
    });
    for (const Task& task : tasks) {
        cout << setw(4) << task.service_time << "\t" << setw(3) << task.wait_time << "\n";
    }
}

void simulate_rr(vector<Task>& tasks) {
    queue<Task*> queue;
    vector<Task*> all_tasks;
    vector<Task>::size_type task_index = 0;

    int time = 0;
    const int time_quantum = 1; 
    Task* current_task = nullptr;
    int time_slice = 0; 

    // Sorting tasks by arrival time
    sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
        return a.arrival_time < b.arrival_time;
    });
    

    cout << "RR scheduling results (time slice is 1)\n\n";
    cout << "time   cpu   ready queue (tid/rst)\n";
    cout << "----   ---   ---------------------\n";

    // Driver loop for RR
    while (task_index < tasks.size() || !queue.empty() || current_task != nullptr) {
        // Add tasks if they arrived
        while (task_index < tasks.size() && tasks[task_index].arrival_time <= time) {
            queue.push(&tasks[task_index]);
            all_tasks.push_back(&tasks[task_index]);
            task_index++;
        }

        // Fetch next task if CPU is idle
        if (current_task == nullptr || current_task->remaining_time == 0 || time_slice == 0) {
            if (current_task != nullptr && current_task->remaining_time > 0) {
                queue.push(current_task);
            }
            if (!queue.empty()) {
                current_task = queue.front();
                queue.pop();
                if (current_task->start_time == -1) {
                    current_task->start_time = time;
                }
                time_slice = time_quantum;
            } else {
                current_task = nullptr;
            }
        }

        // Stop printing time if all tasks are completed
        if (queue.empty() && current_task == nullptr && task_index >= tasks.size()) {
            break;
        }

        cout << setw(3) << time;
        if (current_task) {
            cout << setw(5) << current_task->id << current_task->remaining_time;
        } else {
            cout << setw(10);
        }

        
        cout << "    ";
        if (queue.empty()) {
            cout << "--";
        } else {
            std::queue<Task*> temp_queue = queue;
            while (!temp_queue.empty()) {
                Task* task = temp_queue.front();
                temp_queue.pop();
                cout << task->id << task->remaining_time;
                if (!temp_queue.empty()) cout << ", ";
            }
        }
        cout << endl;

        if (current_task != nullptr) {
            current_task->remaining_time--;
            time_slice--;

            if (current_task->remaining_time == 0) {
                current_task->completion_time = time + 1;

                if (current_task->response_time == 0) {
                    current_task->response_time = current_task->completion_time - current_task->arrival_time;
                }
                current_task->wait_time = current_task->completion_time - current_task->arrival_time - current_task->service_time;
            }
        }

        time++;
    }

    // Menu output
    cout << "\n     arrival service completion response wait";
    cout << "\ntid   time    time      time      time   time";
    cout << "\n---  ------- ------- ---------- -------- ----\n";
    for (const Task& task : tasks) {
        cout << " " << task.id << setw(7)
             << task.arrival_time << setw(8)
             << task.service_time << setw(10)
             << task.completion_time << setw(10)
             << task.response_time << setw(7)
             << task.wait_time << "\n";
    }
    cout << "\nservice wait\n time   time\n";
    cout << "------- ----\n";

    sort(tasks.begin(), tasks.end(), [](const Task& a, const Task& b) {
        if (a.service_time == b.service_time) return a.arrival_time < b.arrival_time;
        return a.service_time < b.service_time;
    });
    for (const Task& task : tasks) {
        cout << setw(4) << task.service_time << "\t" << setw(3) << task.wait_time << "\n";
    }
}