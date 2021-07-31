#include "torch_ipex/csrc/cpu/CustomOPs.h"
#include "Conv.h"
#include "LayerNorm.h"
#include "Linear.h"
#include "Matmul.h"
#include "Pooling.h"
#include "Softmax.h"
#include "torch_ipex/csrc/utils.h"

#include <ATen/Context.h>
#include <ATen/InferSize.h>
#include <c10/util/Exception.h>
#include <c10/util/Logging.h>
#include <torch/csrc/autograd/function.h>

#include <limits>

#include "ideep/ideep.hpp"

namespace torch_ipex {
namespace cpu {

at::Tensor AtenIpexJITDev::dil_convolution_base(
    const at::Tensor& input,
    const at::Tensor& weight,
    const at::Tensor& bias,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    int64_t groups) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_convolution_base", std::vector<c10::IValue>({}));
#endif
  return convolution_impl(input, weight, bias, stride, padding, dilation, groups, ideep::attr_t());
}

at::Tensor AtenIpexJITDev::dil_convolution_swish(
    const at::Tensor& input,
    const at::Tensor& weight,
    const at::Tensor& bias,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    int64_t groups) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_convolution_swish", std::vector<c10::IValue>({}));
#endif
  return convolution_impl(
    input,
    weight,
    bias,
    stride,
    padding,
    dilation,
    groups,
    ideep::attr_t::fuse_swish());
}

at::Tensor AtenIpexJITDev::dil_convolution_sigmoid(
    const at::Tensor& input,
    const at::Tensor& weight,
    const at::Tensor& bias,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    int64_t groups) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_convolution_sigmoid", std::vector<c10::IValue>({}));
#endif
  return convolution_impl(
    input,
    weight,
    bias,
    stride,
    padding,
    dilation,
    groups,
    ideep::attr_t::fuse_sigmoid());
}

/**
 * Dispatch at::matmul + at::div pattern to ipex for jit inference, but only one-element 
 * tensor and channel dim boadcast is enabled in oneDNN 2.2.0 now. So, for simplicity,this path is just 
 * a fallback path now.
 * output(out) = (tensor1 * tensor2).div(div_input)
 *  
 * @param tensor1 
 * @param tensor2 
 * @param out Optinal output provided by user for matmul 
 * @param div_input Input Tensor for div 
 * @return Value for the fusion pattern output. 
 */
at::Tensor  AtenIpexJITDev::dil_matmul_div(
    const at::Tensor& tensor1,
    const at::Tensor& tensor2,
    at::Tensor out,
    const at::Tensor& div_input) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_matmul_div_fallback", std::vector<c10::IValue>({}));
#endif
  if (out.defined()) {
    at::matmul_out(out, tensor1, tensor2);
    return out.div(div_input);
  } 
  auto output = at::matmul(tensor1, tensor2);
  return output.div(div_input);
      
 
}

/**
 *Dispatch at::matmul + at::div pattern to ipex for jit inference, but only bmm with same shape for 
 *tensor1 and tensor2 and scalar input for div will be dispatched to oneDNN kernel. Otherwise will fallback.
 *For oneDNN kernel, scalar input will be used as the scale attribute for matmul primitive.
 *output(out) = (tensor1 * tensor2).div(div_input_scalar).
 *ToDo: matmul + div scalar for matmul with other shape  
 *
 *@param tensor1
 *@param tensor2
 *@param out Optinal output provided by user for matmul
 *@param div_input Input scalar for div
 *@return Value for the fusion pattern output.
 */
at::Tensor  AtenIpexJITDev::dil_matmul_div(
    const at::Tensor& tensor1,
    const at::Tensor& tensor2,
    at::Tensor out,
    const c10::Scalar& div_input) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_matmul_div_scalar", std::vector<c10::IValue>({}));
#endif
  auto dim_tensor1 = tensor1.dim();
  auto dim_tensor2 = tensor2.dim();
  if (dim_tensor1 == dim_tensor2 && dim_tensor1 >= 3) { 
    float scale = 1.0 / div_input.to<float>(); 
    return bmm_impl(tensor1, tensor2, out, ideep::attr_t(), scale);
  } else {
    return AtenIpexJITDev::dil_matmul_div(tensor1, tensor2, out, at::native::wrapped_scalar_tensor(div_input));
  }
}

at::Tensor AtenIpexJITDev::dil_convolution_clamp(
    const at::Tensor& input,
    const at::Tensor& weight,
    const at::Tensor& bias,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    int64_t groups,
    float lower_bound,
    float upper_bound) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_convolution_clamp", std::vector<c10::IValue>({}));
#endif
  return convolution_impl(
    input,
    weight,
    bias,
    stride,
    padding,
    dilation,
    groups,
    ideep::attr_t::fuse_clamp(lower_bound, upper_bound));
}

at::Tensor AtenIpexJITDev::dil_convolution_relu(
    const at::Tensor& input,
    const at::Tensor& weight,
    const at::Tensor& bias,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    int64_t groups) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_convolution_relu", std::vector<c10::IValue>({}));
#endif
  return convolution_impl(
    input,
    weight,
    bias,
    stride,
    padding,
    dilation,
    groups,
    ideep::attr_t::fuse_relu());
}

at::Tensor AtenIpexJITDev::dil_convolution_elu(
    const at::Tensor& input,
    const at::Tensor& weight,
    const at::Tensor& bias,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    int64_t groups,
    float alpha,
    at::Scalar scale,
    at::Scalar input_scale) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_convolution_elu", std::vector<c10::IValue>({}));
#endif
  auto scale_value = scale.to<float>();
  auto input_scale_value = input_scale.to<float>();
  return convolution_impl(
    input,
    weight,
    bias,
    stride,
    padding,
    dilation,
    groups,
    ideep::attr_t::fuse_elu(scale_value, alpha, input_scale_value));
}

at::Tensor& AtenIpexJITDev::dil_convolution_sum(
    const at::Tensor& input,
    const at::Tensor& weight,
    const at::Tensor& bias,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    int64_t groups,
    at::Tensor& accumu,
    at::Scalar alpha) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_convolution_sum", std::vector<c10::IValue>({}));
#endif
  auto scale = alpha.to<float>();
  convolution_inplace_impl(
    input,
    weight,
    bias,
    accumu,
    stride,
    padding,
    dilation,
    groups,
    ideep::attr_t::fuse_sum(scale));
  return accumu;
}

at::Tensor& AtenIpexJITDev::dil_convolution_sum_relu(
    const at::Tensor& input,
    const at::Tensor& weight,
    const at::Tensor& bias,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    int64_t groups,
    at::Tensor& accumu,
    at::Scalar alpha) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_convolution_sum_relu", std::vector<c10::IValue>({}));
#endif
  auto scale = alpha.to<float>();
  convolution_inplace_impl(
    input,
    weight,
    bias,
    accumu,
    stride,
    padding,
    dilation,
    groups,
    ideep::attr_t::residual(scale));
  return accumu;
}

at::Tensor AtenIpexJITDev::dil_max_pool2d(
    const at::Tensor& input,
    at::IntArrayRef kernel_size,
    at::IntArrayRef stride,
    at::IntArrayRef padding,
    at::IntArrayRef dilation,
    bool ceil_mode) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_max_pool2d", std::vector<c10::IValue>({}));
#endif
  TORCH_CHECK(std::all_of(dilation.cbegin(), dilation.cend(), [](int64_t i) { return 1 == i; }),
      "dil_max_pool2d does not support dilation case");
  return pooling_impl(
      input,
      kernel_size,
      stride,
      padding,
      dilation,
      ceil_mode,
      ideep::algorithm::pooling_max);
}

at::Tensor AtenIpexJITDev::dil_linear(
    const at::Tensor& self,
    const at::Tensor& weight,
    const at::Tensor& bias) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_linear", std::vector<c10::IValue>({}));
#endif
  return linear_impl(self, weight, bias, ideep::attr_t());
}

at::Tensor AtenIpexJITDev::dil_linear_fuse_eltwise(
    const at::Tensor& self,
    const at::Tensor& weight,
    const at::Tensor& bias,
    const ideep::attr_t& attr) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_linear_fuse_eltwise", std::vector<c10::IValue>({}));
#endif
  return linear_impl(self, weight, bias, attr);
}


/**
 *Dispatch Linear + Add fusion pattern to ipex oneDNN kernel for inference mode.
 *This feature might improve performance for cases like residual learning blocks
 *Pattern: accum = accum * alpha + Linear(self, weight, bias) 
 *
 *@param self Activatin input for Linear  
 *@param weight Weight for Linear
 *@param bias Bias for Linear
 *@param accum One input for add operation, another is the output of Linear
 *@param alpha Scale for accum when doing add operation. 
 *
 *@return Value for the fusion pattern output. 
 */
at::Tensor AtenIpexJITDev::dil_linear_add(
    const at::Tensor& self, 
    const at::Tensor& weight, 
    const at::Tensor& bias, 
    at::Tensor& accumu, 
    at::Scalar alpha) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_linear_add", std::vector<c10::IValue>({}));
#endif
  auto scale = alpha.to<float>();
  return linear_inplace_impl(self, weight, bias, accumu, ideep::attr_t::fuse_sum(scale));
}

//Dispatch softmax to oneDNN path for jit inference
at::Tensor AtenIpexJITDev::dil_softmax(
    const at::Tensor& input,
    const int64_t dim,
    const at::IValue& dtype) {
#if defined(IPEX_PROFILE_OP)
  RECORD_FUNCTION("AtenIpexJITDev::dil_softmax", std::vector<c10::IValue>({}));
#endif
  auto half_to_float = false;
  if (!dtype.isNone()) {
    auto outtype = dtype.toScalarType();
    auto intype = input.scalar_type();
    AT_ASSERTM(
      intype != at::ScalarType::Half,
      "softmax with half to float conversion is not supported on Mkldnn");
    at::Tensor converted = input.toType(outtype);
    return softmax_impl(converted, dim);
  }

  return softmax_impl(input, dim);
}

/**
 *prepare inputs for dil_layernorm
 *
 *@param input: the source tensor to layernorm
 *@param normalized_shape: input shape from an expected input of size
 *@param weight: scale tensor for layernorm
 *@param bias: shift tensor for layernorm
 *
 *@return inputs for dil_layernorm.
 **/
std::tuple<at::Tensor, at::Tensor, at::Tensor, int64_t, int64_t>
_prepare_layer_norm_inputs(const at::Tensor &input,
                           at::IntArrayRef normalized_shape,
                           const at::Tensor &weight /* optional */,
                           const at::Tensor &bias /* optional */) {

  const int normalized_ndim = normalized_shape.size();
  TORCH_CHECK(normalized_ndim >= 1,
              "Expected normalized_shape to be at least 1-dimensional, i.e., ",
              "containing at least one element, but got normalized_shape = ",
              normalized_shape);
  TORCH_CHECK(
      !weight.defined() || weight.sizes().equals(normalized_shape),
      "Expected weight to be of same shape as normalized_shape, but got ",
      "weight of shape ", weight.sizes(),
      " and normalized_shape = ", normalized_shape);
  TORCH_CHECK(!bias.defined() || bias.sizes().equals(normalized_shape),
              "Expected bias to be of same shape as normalized_shape, but got ",
              "bias of shape ", bias.sizes(),
              " and normalized_shape = ", normalized_shape);

  const auto input_shape = input.sizes();
  const auto input_ndim = input.dim();

  if (input_ndim < normalized_ndim ||
      !input_shape.slice(input_ndim - normalized_ndim)
           .equals(normalized_shape)) {
    std::stringstream ss;
    ss << "Given normalized_shape=" << normalized_shape
       << ", expected input with shape [*";
    for (auto size : normalized_shape) {
      ss << ", " << size;
    }
    ss << "], but got input of size" << input_shape;
    AT_ERROR(ss.str());
  }

  const int axis = input_ndim - normalized_ndim;
  const int64_t M =
      std::accumulate(input_shape.cbegin(), input_shape.cbegin() + axis,
                      static_cast<int64_t>(1), std::multiplies<int64_t>());
  const int64_t N =
      std::accumulate(input_shape.cbegin() + axis, input_shape.cend(),
                      static_cast<int64_t>(1), std::multiplies<int64_t>());
  ;

  const auto &X = input.is_contiguous() ? input : input.contiguous();
  const auto &gamma = weight.is_contiguous() ? weight : weight.contiguous();
  const auto &beta = bias.is_contiguous() ? bias : bias.contiguous();
  return std::make_tuple(X, gamma, beta, M, N);
}

/**
 * at::layer_norm performance drop due to
 * #PR https://github.com/pytorch/pytorch/pull/59987
 * This is a workaround for layernorm regression.
 * Replace at::layer_norm with ipex::layernorm in jit pass for inference.
 * Now, we only use oneDNN kernel when both weight and bias are provided.
 * ToDo: more scenarios to use oneDNN or remvoe this pass
 * when at::layer_norm performance is back compared to w/o
 * mergeing https://github.com/pytorch/pytorch/pull/59987
 *
 * @param input: the source tensor to layernorm
 * @param normalized_shape: input shape from an expected input of size
 * @param weight_opt: scale tensor for layernorm
 * @param bias_opt: shift tensor for layernorm
 * @param bias: a value added to the denominator for numerical stability.
 * Default: 1e-5
 *
 * return: output for layernorm
 */
at::Tensor AtenIpexJITDev::dil_layernorm(
    const at::Tensor &input, at::IntArrayRef normalized_shape,
    const c10::optional<at::Tensor> &weight_opt,
    const c10::optional<at::Tensor> &bias_opt, float eps, bool cudnn_enable) {

  if (weight_opt.has_value() && bias_opt.has_value()) {
#if defined(IPEX_PROFILE_OP)
    RECORD_FUNCTION("AtenIpexJITDev::dil_layernorm",
                    std::vector<c10::IValue>({}));
#endif
    auto inputs = _prepare_layer_norm_inputs(
        input, normalized_shape, weight_opt.value(), bias_opt.value());
    auto X = std::get<0>(inputs);
    auto gamma = std::get<1>(inputs);
    auto beta = std::get<2>(inputs);
    auto M = std::get<3>(inputs);
    auto N = std::get<4>(inputs);
    return std::get<0>(dil_native_layer_norm_impl(X, gamma, beta, M, N, eps));
  }
  c10::MaybeOwned<at::Tensor> weight_maybe_owned =
      at::borrow_from_optional_tensor(weight_opt);
  const at::Tensor &weight = *weight_maybe_owned;
  c10::MaybeOwned<at::Tensor> bias_maybe_owned =
      at::borrow_from_optional_tensor(bias_opt);
  const at::Tensor &bias = *bias_maybe_owned;
  return std::get<0>(
      at::native_layer_norm(input, normalized_shape, weight, bias, eps));
}

}  // namespace cpu
}  // namespace torch_ipex