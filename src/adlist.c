/* adlist.c - A generic doubly linked list implementation
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <stdlib.h>
#include "adlist.h"
#include "zmalloc.h"

/*
 创建一个新的链表。创建完成的链表可以用AlFreeList()释放
 但是每个链表节点的私有值 需要在调用AlFreeList()之前由用户自己释放
*/
/* Create a new list. The created list can be freed with
 * AlFreeList(), but private value of every node need to be freed
 * by the user before to call AlFreeList().
 *
 * On error, NULL is returned. Otherwise the pointer to the new list. */
list *listCreate(void)
{
    struct list *list;

    //分配内存失败就返回NULL
    if ((list = zmalloc(sizeof(*list))) == NULL)
        return NULL;
    //头节点 和尾节点都为空    
    list->head = list->tail = NULL;
    //长度为0
    list->len = 0;
    //
    list->dup = NULL;
    list->free = NULL;
    list->match = NULL;
    return list;
}

/**
 * 释放整个节点
 * 
 */
/* Free the whole list.
 *
 * This function can't fail. */
void listRelease(list *list)
{
    unsigned long len;
    listNode *current, *next;

    //先获取头节点
    current = list->head;
    //链表长度
    len = list->len;
    //根据长度循环
    while(len--) {
        next = current->next;
        //调用链表的free函数，释放当前节点的值
        if (list->free) list->free(current->value);
        //释放当前节点
        zfree(current);
        //当前变成下个节点
        current = next;
    }
    zfree(list);
}

/* Add a new node to the list, to head, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeHead(list *list, void *value)
{
    listNode *node;
    //为节点分配内存
    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
        
    //赋予节点值
    node->value = value;
    if (list->len == 0) {
        //如果当前链表长度为0 
        //那么头节点和尾节点都指向这个节点
        list->head = list->tail = node;
        //当前这个节点的prev和next都为空
        node->prev = node->next = NULL;
    } else {
        //当前节点的prev为空
        node->prev = NULL;
        //当前节点的下个节点指向原来的头节点
        node->next = list->head;
        //原来的头节点的prev节点变成当前节点
        list->head->prev = node;
        //链表的头节点变成当前节点
        list->head = node;
    }
    //链表长度增加
    list->len++;
    return list;
}

/* Add a new node to the list, to tail, containing the specified 'value'
 * pointer as value.
 *
 * On error, NULL is returned and no operation is performed (i.e. the
 * list remains unaltered).
 * On success the 'list' pointer you pass to the function is returned. */
list *listAddNodeTail(list *list, void *value)
{
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
        
    //赋予节点值
    node->value = value;
    if (list->len == 0) {
        //头节点和尾节点都是当前节点
        list->head = list->tail = node;
        node->prev = node->next = NULL;
    } else {
        //当前节点的prev节点变成原来的尾节点
        node->prev = list->tail;
        //当前节点的next节点为空
        node->next = NULL;
        //原来尾节点的下个节点为 当前节点
        list->tail->next = node;
        //链表的尾节点变成当前这个节点
        list->tail = node;
    }
    //链表长度增加
    list->len++;
    return list;
}
/**
 * 插入节点
 */
list *listInsertNode(list *list, listNode *old_node, void *value, int after) {
    listNode *node;

    if ((node = zmalloc(sizeof(*node))) == NULL)
        return NULL;
    //节点值
    node->value = value;

    //插入老节点的后面
    if (after) {
        //原来的旧节点变成当前节点的上个节点
        node->prev = old_node;
        //原来的旧节点的下个节点变成当前节点的下个节点
        node->next = old_node->next;
        //如果旧的节点是原来的尾节点 ，那么当前节点 变为尾节点
        if (list->tail == old_node) {
            list->tail = node;
        }
    } else {
        //插入老节点的前面
        //老节点变成当前节点的下个节点
        node->next = old_node;
        //老节点的上个节点变成当前节点的上个节点
        node->prev = old_node->prev;
        //如果老节点是链表的头节点，那么当前节点就变成头节点
        if (list->head == old_node) {
            list->head = node;
        }
    }
    //如果当前节点有前置节点，那么前置节点的下个节点就变成当前这个节点
    if (node->prev != NULL) {
        node->prev->next = node;
    }
    //如果当前节点有后置节点，那么后置节点的上个节点就变成当前这个节点
    if (node->next != NULL) {
        node->next->prev = node;
    }
    //链表的数量增加
    list->len++;
    return list;
}

/*
删除指定节点
*/
/* Remove the specified node from the specified list.
 * It's up to the caller to free the private value of the node.
 *
 * This function can't fail. */
void listDelNode(list *list, listNode *node)
{
    //如果有前置节点，那么当前删除节点的下个节点就变为前置节点的下个节点
    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next; //节点的下个节点就变成链表的头节点
    
    //如果有后置节点，那么当前删除节点的上个节点就变为后置节点的上个节点
    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev; //节点的上个节点就变成链表的尾节点，代表当前删除节点是原来的尾节点
    //如果有释放函数 ，那就释放当前删除节点的值
    if (list->free) list->free(node->value);
    //释放当前节点
    zfree(node);
    //链表数量减少一个
    list->len--;
}

//返回链表的迭代器
/* Returns a list iterator 'iter'. After the initialization every
 * call to listNext() will return the next element of the list.
 *
 * This function can't fail. */
listIter *listGetIterator(list *list, int direction)
{
    listIter *iter;

    if ((iter = zmalloc(sizeof(*iter))) == NULL) return NULL;

    //从头节点开始
    if (direction == AL_START_HEAD)
        iter->next = list->head; //链表的头节点变为迭代器的next
    else
        iter->next = list->tail;  //链表的尾节点变为迭代器的next
    iter->direction = direction; //赋值迭代方向
    return iter;
}

/* Release the iterator memory */
void listReleaseIterator(listIter *iter) {
    zfree(iter);
}

/* Create an iterator in the list private iterator structure */
void listRewind(list *list, listIter *li) {
    //链表头节点变为迭代器的next
    li->next = list->head;
    li->direction = AL_START_HEAD;
}

void listRewindTail(list *list, listIter *li) {
    li->next = list->tail;
    li->direction = AL_START_TAIL;
}

/* Return the next element of an iterator.
 * It's valid to remove the currently returned element using
 * listDelNode(), but not to remove other elements.
 *
 * The function returns a pointer to the next element of the list,
 * or NULL if there are no more elements, so the classical usage patter
 * is:
 *
 * iter = listGetIterator(list,<direction>);
 * while ((node = listNext(iter)) != NULL) {
 *     doSomethingWith(listNodeValue(node));
 * }
 *
 * */
listNode *listNext(listIter *iter)
{
    //获取当前节点
    listNode *current = iter->next;

    //如果当前节点不为空
    if (current != NULL) {
        if (iter->direction == AL_START_HEAD)
            iter->next = current->next; //迭代器的next指为当前节点的下个节点
        else
            iter->next = current->prev;//迭代器的next指为当前节点的上个节点
    }
    return current;
}

/* Duplicate the whole list. On out of memory NULL is returned.
 * On success a copy of the original list is returned.
 *
 * The 'Dup' method set with listSetDupMethod() function is used
 * to copy the node value. Otherwise the same pointer value of
 * the original node is used as value of the copied node.
 *
 * The original list both on success or error is never modified. */
list *listDup(list *orig)
{
    list *copy;
    listIter iter;
    listNode *node;

    //先创建一个列表
    if ((copy = listCreate()) == NULL)
        return NULL;
    
    //函数复制
    copy->dup = orig->dup;
    copy->free = orig->free;
    copy->match = orig->match;
    //iter 直接可以用了。。
    listRewind(orig, &iter);
    //开始迭代
    while((node = listNext(&iter)) != NULL) {
        void *value;

        if (copy->dup) {
            value = copy->dup(node->value);
            if (value == NULL) {
                listRelease(copy);
                return NULL;
            }
        } else
            value = node->value;

        //把值添加到链表最后面
        if (listAddNodeTail(copy, value) == NULL) {
            listRelease(copy);
            return NULL;
        }
    }
    return copy;
}

/* Search the list for a node matching a given key.
 * The match is performed using the 'match' method
 * set with listSetMatchMethod(). If no 'match' method
 * is set, the 'value' pointer of every node is directly
 * compared with the 'key' pointer.
 *
 * On success the first matching node pointer is returned
 * (search starts from head). If no matching node exists
 * NULL is returned. */
listNode *listSearchKey(list *list, void *key)
{
    listIter iter;
    listNode *node;

    //迭代器指向链表头节点
    listRewind(list, &iter);
    while((node = listNext(&iter)) != NULL) {
        //如果有匹配函数
        if (list->match) {
            if (list->match(node->value, key)) {
                return node;
            }
        } else {
            //没有匹配函数就 直接跟节点value做比较
            if (key == node->value) {
                return node;
            }
        }
    }
    return NULL;
}

/* Return the element at the specified zero-based index
 * where 0 is the head, 1 is the element next to head
 * and so on. Negative integers are used in order to count
 * from the tail, -1 is the last element, -2 the penultimate
 * and so on. If the index is out of range NULL is returned. */
listNode *listIndex(list *list, long index) {
    listNode *n;

    //从尾部开始遍历
    if (index < 0) {
        index = (-index)-1;
        n = list->tail;
        while(index-- && n) n = n->prev;
    } else {
        n = list->head;
        while(index-- && n) n = n->next;
    }
    return n;
}

/* Rotate the list removing the tail node and inserting it to the head. */
void listRotate(list *list) {

    //获取尾部节点
    listNode *tail = list->tail;

    if (listLength(list) <= 1) return;

    /* Detach current tail */
    list->tail = tail->prev;
    list->tail->next = NULL;

    //原来尾部节点变成头节点
    /* Move it as head */

    /*原来头部节点的前节点指向 tail*/
    list->head->prev = tail;
    tail->prev = NULL;

    //原来tail的next指向 原来的头部节点
    tail->next = list->head;

    //头部指向原来尾部的节点
    list->head = tail;
}
