#ifndef LOCKFREEQUEUE_LOCKFREEQUEUE_H
#define LOCKFREEQUEUE_LOCKFREEQUEUE_H


#include <stdint.h>
#include <memory>
#include <atomic>
#include <iostream>

template<typename T>
class LockFreeQueue {

private:
    struct Node;
    struct CountedNode {
        CountedNode() noexcept : node(nullptr), external_counter(0)   {    }
        CountedNode(const T& data) noexcept : external_counter(0), node(new Node(data)) {}
        CountedNode(Node* node) noexcept : external_counter(0), node(node) {}
        int64_t external_counter;
        Node* node;

        bool operator!=(const CountedNode& rhs){
            return rhs.node != this->node || rhs.external_counter != this->external_counter;
        }
    };
    struct Node {
        Node(const T& data) : data(new T(data)), next(CountedNode()), internal_counter(0), tail_external(0) { }
        Node() : data(nullptr), next(CountedNode()), internal_counter(0), tail_external(0) {}
        std::atomic<T*> data;
        std::atomic<CountedNode> next;
        std::atomic<int64_t> internal_counter;
        std::atomic<int64_t> tail_external;

        ~Node() {
            delete this->data.load();
        }
    };
    std::atomic<CountedNode> head;
    std::atomic<CountedNode> tail;

public:
    LockFreeQueue()  {
        Node* node = new Node();
        CountedNode cn(node);
        head.store(cn);
        tail.store(cn);
    }
    ~LockFreeQueue() {
        CountedNode iter = head.load(), empty;
        Node *toRemoveN;
         while( iter != empty){
             toRemoveN = iter.node;
             iter = iter.node->next.load();
             delete toRemoveN;
         }
    }

    void push(const T& data) {
        CountedNode newNode = CountedNode(data), currTail, incrementedCurrTail;
        while(true) {
            CountedNode emptyNode;
            currTail = tail.load(std::memory_order_relaxed);
            do {
                incrementedCurrTail = currTail;
                incrementedCurrTail.external_counter++;
            } while (!tail.compare_exchange_strong(currTail, incrementedCurrTail, std::memory_order_release, std::memory_order_acquire ));
             if (currTail.node->next.compare_exchange_strong(emptyNode, newNode, std::memory_order_release, std::memory_order_acquire )) {
                moveNext(incrementedCurrTail);
                return;
            }
            else {
                moveNext(incrementedCurrTail);
            }
        }
    }
    void moveNext(CountedNode& currTail) {
        CountedNode next = currTail.node->next.load();
        Node* node = currTail.node;
        int64_t curr = currTail.node->tail_external.load();

        do {
            if( currTail.external_counter <= curr) break;
        } while( !currTail.node->tail_external.compare_exchange_strong(curr, currTail.external_counter));

        tail.compare_exchange_strong(currTail, next);
        decreaseNode(node);
        return;
    }

    void popNode(CountedNode &countedNode) {
        int64_t sum = countedNode.node->tail_external.load() + countedNode.external_counter;
        countedNode.node->internal_counter += sum;
        decreaseNode(countedNode.node);
    }
    void decreaseNode(Node *node) {
        if( node->internal_counter.fetch_sub(1) == 1)
            delete node;
    }
    std::unique_ptr<T> pop() {
        CountedNode currHead, incrementedCurrHead, incrementedNext;
        while(true) {
            CountedNode next;
            currHead = head.load(std::memory_order::memory_order_relaxed);
            do {
                incrementedCurrHead = currHead;
                incrementedCurrHead.external_counter++;
            } while (!head.compare_exchange_strong(currHead, incrementedCurrHead, std::memory_order_release, std::memory_order_acquire));

             CountedNode copyIncrementedHead = incrementedCurrHead;

            if( currHead.node != tail.load(std::memory_order_relaxed).node){
                next = currHead.node->next;
                next.external_counter = 1;
                if (head.compare_exchange_strong(incrementedCurrHead, next, std::memory_order_release, std::memory_order_acquire)) {
                    T* ptr = next.node->data.exchange(nullptr, std::memory_order::memory_order_acquire);
                    decreaseNode(next.node);
                    popNode(copyIncrementedHead);
                    return std::unique_ptr<T>(ptr);
                } else {
                    decreaseNode(copyIncrementedHead.node);
                }
            } else {
                decreaseNode(copyIncrementedHead.node);
                return std::unique_ptr<T>();
            }
        }
    }
};


#endif 

