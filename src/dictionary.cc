/**
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "dictionary.h"

#include <assert.h>

#include <iostream>
#include <fstream>
#include <algorithm>
#include <iterator>

namespace fasttext {

const std::string Dictionary::EOS = "</s>";
const std::string Dictionary::BOW = "<";
const std::string Dictionary::EOW = ">";

Dictionary::Dictionary(std::shared_ptr<Args> args) : args_(args),
  word2int_(MAX_VOCAB_SIZE, -1), size_(0), nwords_(0), nlabels_(0),
  ntokens_(0), quant_(false) {}

int32_t Dictionary::find(const std::string& w) const {
  int32_t h = hash(w) % MAX_VOCAB_SIZE;
  while (word2int_[h] != -1 && words_[word2int_[h]].word != w) {
    h = (h + 1) % MAX_VOCAB_SIZE;
  }
  return h;
}

void Dictionary::add(const std::string& w) {
  int32_t h = find(w);
  ntokens_++;
  if (word2int_[h] == -1) {
    entry e;
    e.word = w;
    e.count = 1;
    e.type = (w.find(args_->label) == 0) ? entry_type::label : entry_type::word;
    words_.push_back(e);
    word2int_[h] = size_++;
  } else {
    words_[word2int_[h]].count++;
  }
}

int32_t Dictionary::nwords() const {
  return nwords_;
}

int32_t Dictionary::nlabels() const {
  return nlabels_;
}

int64_t Dictionary::ntokens() const {
  return ntokens_;
}

const std::vector<int32_t>& Dictionary::getNgrams(int32_t i) const {
  assert(i >= 0);
  assert(i < nwords_);
  return words_[i].subwords;
}

const std::vector<int32_t> Dictionary::getNgrams(const std::string& word) const {
  int32_t i = getId(word);
  if (i >= 0) {
    return getNgrams(i);
  }
  std::vector<int32_t> ngrams;
  computeNgrams(BOW + word + EOW, ngrams);
  return ngrams;
}

void Dictionary::getNgrams(const std::string& word,
                           std::vector<int32_t>& ngrams,
                           std::vector<std::string>& substrings) const {
  int32_t i = getId(word);
  ngrams.clear();
  substrings.clear();
  if (i >= 0) {
    ngrams.push_back(i);
    substrings.push_back(words_[i].word);
  } else {
    ngrams.push_back(-1);
    substrings.push_back(word);
  }
  computeNgrams(BOW + word + EOW, ngrams, substrings);
}

bool Dictionary::discard(int32_t id, real rand) const {
  assert(id >= 0);
  assert(id < nwords_);
  if (args_->model == model_name::sup) return false;
  return rand > pdiscard_[id];
}

int32_t Dictionary::getId(const std::string& w) const {
  int32_t h = find(w);
  return word2int_[h];
}

entry_type Dictionary::getType(int32_t id) const {
  assert(id >= 0);
  assert(id < size_);
  return words_[id].type;
}

std::string Dictionary::getWord(int32_t id) const {
  assert(id >= 0);
  assert(id < size_);
  return words_[id].word;
}

uint32_t Dictionary::hash(const std::string& str) const {
  uint32_t h = 2166136261;
  for (size_t i = 0; i < str.size(); i++) {
    h = h ^ uint32_t(str[i]);
    h = h * 16777619;
  }
  return h;
}

void Dictionary::computeNgrams(const std::string& word,
                               std::vector<int32_t>& ngrams,
                               std::vector<std::string>& substrings) const {
  for (size_t i = 0; i < word.size(); i++) {
    std::string ngram;
    if ((word[i] & 0xC0) == 0x80) continue;
    for (size_t j = i, n = 1; j < word.size() && n <= args_->maxn; n++) {
      ngram.push_back(word[j++]);
      while (j < word.size() && (word[j] & 0xC0) == 0x80) {
        ngram.push_back(word[j++]);
      }
      if (n >= args_->minn && !(n == 1 && (i == 0 || j == word.size()))) {
        int32_t h = hash(ngram) % args_->bucket;
        ngrams.push_back(nwords_ + h);
        substrings.push_back(ngram);
      }
    }
  }
}

void Dictionary::computeNgrams(const std::string& word,
                               std::vector<int32_t>& ngrams) const {
  for (size_t i = 0; i < word.size(); i++) {
    std::string ngram;
    if ((word[i] & 0xC0) == 0x80) continue;
    for (size_t j = i, n = 1; j < word.size() && n <= args_->maxn; n++) {
      ngram.push_back(word[j++]);
      while (j < word.size() && (word[j] & 0xC0) == 0x80) {
        ngram.push_back(word[j++]);
      }
      if (n >= args_->minn && !(n == 1 && (i == 0 || j == word.size()))) {
        int32_t h = hash(ngram) % args_->bucket;
        ngrams.push_back(nwords_ + h);
      }
    }
  }
}

void Dictionary::initNgrams() {
  for (size_t i = 0; i < size_; i++) {
    std::string word = BOW + words_[i].word + EOW;
    words_[i].subwords.push_back(i);
    computeNgrams(word, words_[i].subwords);
  }
}

bool Dictionary::readWord(std::istream& in, std::string& word) const
{
  char c;
  std::streambuf& sb = *in.rdbuf();
  word.clear();
  while ((c = sb.sbumpc()) != EOF) {
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\f' || c == '\0') {
      if (word.empty()) {
        if (c == '\n') {
          word += EOS;
          return true;
        }
        continue;
      } else {
        if (c == '\n')
          sb.sungetc();
        return true;
      }
    }
    word.push_back(c);
  }
  // trigger eofbit
  in.get();
  return !word.empty();
}

void Dictionary::readFromFile(std::istream& in) {
  std::string word;
  int64_t minThreshold = 1;
  while (readWord(in, word)) {
    add(word);
    if (ntokens_ % 1000000 == 0 && args_->verbose > 1) {
      std::cout << "\rRead " << ntokens_  / 1000000 << "M words" << std::flush;
    }
    if (size_ > 0.75 * MAX_VOCAB_SIZE) {
      minThreshold++;
      threshold(minThreshold, minThreshold);
    }
  }
  threshold(args_->minCount, args_->minCountLabel);
  initTableDiscard();
  initNgrams();
  if (args_->verbose > 0) {
    std::cout << "\rRead " << ntokens_  / 1000000 << "M words" << std::endl;
    std::cout << "Number of words:  " << nwords_ << std::endl;
    std::cout << "Number of labels: " << nlabels_ << std::endl;
  }
  if (size_ == 0) {
    std::cerr << "Empty vocabulary. Try a smaller -minCount value." << std::endl;
    exit(EXIT_FAILURE);
  }
}

void Dictionary::threshold(int64_t t, int64_t tl) {
  sort(words_.begin(), words_.end(), [](const entry& e1, const entry& e2) {
      if (e1.type != e2.type) return e1.type < e2.type;
      return e1.count > e2.count;
    });
  words_.erase(remove_if(words_.begin(), words_.end(), [&](const entry& e) {
        return (e.type == entry_type::word && e.count < t) ||
               (e.type == entry_type::label && e.count < tl);
      }), words_.end());
  words_.shrink_to_fit();
  size_ = 0;
  nwords_ = 0;
  nlabels_ = 0;
  std::fill(word2int_.begin(), word2int_.end(), -1);
  for (auto it = words_.begin(); it != words_.end(); ++it) {
    int32_t h = find(it->word);
    word2int_[h] = size_++;
    if (it->type == entry_type::word) nwords_++;
    if (it->type == entry_type::label) nlabels_++;
  }
}

void Dictionary::initTableDiscard() {
  pdiscard_.resize(size_);
  for (size_t i = 0; i < size_; i++) {
    real f = real(words_[i].count) / real(ntokens_);
    pdiscard_[i] = sqrt(args_->t / f) + args_->t / f;
  }
}

std::vector<int64_t> Dictionary::getCounts(entry_type type) const {
  std::vector<int64_t> counts;
  for (auto& w : words_) {
    if (w.type == type) counts.push_back(w.count);
  }
  return counts;
}

void Dictionary::addNgrams(std::vector<int32_t>& line,
                           const std::vector<int32_t>& hashes,
                           int32_t n) const {
  for (int32_t i = 0; i < hashes.size(); i++) {
    uint64_t h = hashes[i];
    for (int32_t j = i + 1; j < hashes.size() && j < i + n; j++) {
      h = h * 116049371 + hashes[j];
      int64_t id = h % args_->bucket;
      if (quantidx_.size() != 0) {
        if (quantidx_.find(id) != quantidx_.end()) {
          id = quantidx_.at(id);
        } else {continue;}
      }
      line.push_back(nwords_ + id);
    }
  }
}

int32_t Dictionary::getLine(std::istream& in,
                            std::vector<std::string>& tokens) const {
  if (in.eof()) {
    in.clear();
    in.seekg(std::streampos(0));
  }
  tokens.clear();
  std::string token;
  while (readWord(in, token)) {
    tokens.push_back(token);
    if (token == EOS) break;
    if (tokens.size() > MAX_LINE_SIZE && args_->model != model_name::sup) break;
  }
  return tokens.size();
}

int32_t Dictionary::getLine(std::istream& in,
                            std::vector<int32_t>& words,
                            std::vector<int32_t>& word_hashes,
                            std::vector<int32_t>& labels,
                            std::minstd_rand& rng) const {
  std::uniform_real_distribution<> uniform(0, 1);
  std::vector<std::string> tokens;
  getLine(in, tokens);
  words.clear();
  labels.clear();
  word_hashes.clear();
  int32_t ntokens = 0;
  for(auto it = tokens.cbegin(); it != tokens.cend(); ++it) {
    int32_t h = find(*it);
    int32_t wid = word2int_[h];
    if (wid < 0) {
      word_hashes.push_back(hash(*it));
      continue;
    }
    entry_type type = getType(wid);
    ntokens++;
    if (type == entry_type::word && !discard(wid, uniform(rng))) {
      words.push_back(wid);
      word_hashes.push_back(hash(*it));
    }
    if (type == entry_type::label) {
      labels.push_back(wid - nwords_);
    }
  }
  return ntokens;
}


int32_t Dictionary::getLine(std::istream& in,
                            std::vector<int32_t>& words,
                            std::vector<int32_t>& labels,
                            std::minstd_rand& rng) const {
  std::vector<int32_t> word_hashes;
  int32_t ntokens = getLine(in, words, word_hashes, labels, rng);
  if (args_->model == model_name::sup ) {
    if (quant_) {
      addNgrams(words, word_hashes, args_->wordNgrams);
    }
    else {
      std::vector<int32_t> ngrams;
      addNgrams(ngrams, words, args_->wordNgrams);
      words.insert(words.end(), ngrams.begin(), ngrams.end());
    }
  }
  return ntokens;
}

std::string Dictionary::getLabel(int32_t lid) const {
  assert(lid >= 0);
  assert(lid < nlabels_);
  return words_[lid + nwords_].word;
}

void Dictionary::save(std::ostream& out) const {
  out.write((char*) &size_, sizeof(int32_t));
  out.write((char*) &nwords_, sizeof(int32_t));
  out.write((char*) &nlabels_, sizeof(int32_t));
  out.write((char*) &ntokens_, sizeof(int64_t));
  for (int32_t i = 0; i < size_; i++) {
    entry e = words_[i];
    out.write(e.word.data(), e.word.size() * sizeof(char));
    out.put(0);
    out.write((char*) &(e.count), sizeof(int64_t));
    out.write((char*) &(e.type), sizeof(entry_type));
  }
  if (quant_) {
    auto ss = quantidx_.size();
    out.write((char*) &(ss), sizeof(ss));
    for (auto it = quantidx_.begin(); it != quantidx_.end(); it++) {
      out.write((char*)&(it->first), sizeof(int32_t));
      out.write((char*)&(it->second), sizeof(int32_t));
    }
  }
}

void Dictionary::load(std::istream& in) {
  words_.clear();
  std::fill(word2int_.begin(), word2int_.end(), -1);
  in.read((char*) &size_, sizeof(int32_t));
  in.read((char*) &nwords_, sizeof(int32_t));
  in.read((char*) &nlabels_, sizeof(int32_t));
  in.read((char*) &ntokens_, sizeof(int64_t));
  for (int32_t i = 0; i < size_; i++) {
    char c;
    entry e;
    while ((c = in.get()) != 0) {
      e.word.push_back(c);
    }
    in.read((char*) &e.count, sizeof(int64_t));
    in.read((char*) &e.type, sizeof(entry_type));
    words_.push_back(e);
    word2int_[find(e.word)] = i;
  }
  if (quant_) {
    std::size_t size;
    in.read((char*) &size, sizeof(std::size_t));
    for (auto i = 0; i < size; i++) {
      int32_t k, v;
      in.read((char*)&k, sizeof(int32_t));
      in.read((char*)&v, sizeof(int32_t));
      quantidx_[k] = v;
    }
  }
  initTableDiscard();
  initNgrams();
}

void Dictionary::prune(std::vector<int32_t>& idx) {
  std::vector<int32_t> words, ngrams;
  for (auto it = idx.cbegin(); it != idx.cend(); ++it) {
    if (*it < nwords_) {words.push_back(*it);}
    else {ngrams.push_back(*it);}
  }
  std::sort(words.begin(), words.end());
  idx = words;

  if (ngrams.size() != 0) {
    convertNgrams(ngrams);
    idx.insert(idx.end(), ngrams.begin(), ngrams.end());
  }

  std::fill(word2int_.begin(), word2int_.end(), -1);

  int32_t j = 0;
  for (int32_t i = 0; i < words_.size(); i++) {
    if (getType(i) == entry_type::label || (j < words.size() && words[j] == i)) {
      words_[j] = words_[i];
      word2int_[find(words_[j].word)] = j;
      j++;
    }
  }
  nwords_ = words.size();
  size_ = nwords_ +  nlabels_;
  words_.erase(words_.begin() + size_, words_.end());
}

void Dictionary::convertNgrams(std::vector<int32_t>& ngramidx) {

  std::ifstream in(args_->input);
  if (!in.is_open()) {
    std::cerr << "Input file cannot be opened!" << std::endl;
    exit(EXIT_FAILURE);
  }

  std::unordered_map<int32_t, std::unordered_map<int32_t, int32_t>> convertMap;
  for (auto it = ngramidx.cbegin(); it != ngramidx.cend(); ++it) {
   convertMap[*it] = std::unordered_map<int32_t, int32_t>();
  }
  std::vector<std::string> tokens;
  std::vector<int32_t> word_hashes, words, labels, oldhashes, newhashes;
  std::minstd_rand rng;
  while (in.peek() != EOF) {
    getLine(in, words, word_hashes, labels, rng);
    if (words.empty()) {continue;}
    oldhashes.clear(); newhashes.clear();
    addNgrams(oldhashes, words, args_->wordNgrams);
    addNgrams(newhashes, word_hashes, args_->wordNgrams);
    assert(newhashes.size() == oldhashes.size());
    for (int32_t i = 0; i < oldhashes.size(); i++) {
      auto oh = oldhashes[i];
      if (convertMap.find(oh) == convertMap.end()) {continue;}
      convertMap[oh][newhashes[i]]++;
    }
  }
  in.close();

  quantidx_.clear();
  std::vector<int32_t> remaining_indices;
  int32_t size = 0;
  for (auto it = ngramidx.begin(); it != ngramidx.end(); ++it) {
    auto cm = convertMap[*it];
    int32_t newhash; int32_t count = -1;
    for (auto nit = cm.cbegin(); nit != cm.cend(); ++nit) {
      if (count < nit->second) {
        newhash = nit->first;
        count = nit->second;
      }
    }
    newhash -= nwords_;
    if (quantidx_.find(newhash) == quantidx_.end()) {
      quantidx_[newhash] = size;
      size++;
      remaining_indices.push_back(*it);
    }
  }
  ngramidx = remaining_indices;
}

}
